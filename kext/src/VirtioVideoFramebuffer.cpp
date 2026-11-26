#include "VirtioVideoFramebuffer.hpp"

#include <IOKit/IODeviceMemory.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>
#include <pexpert/pexpert.h>

#include "VVBootArgs.hpp"
#include "VVDmaBuffer.hpp"
#include "VVLog.hpp"
#include "VirtioGpuDevice.hpp"
#include "vv/Edid.hpp"
#include "vv/VirtioGpuSpec.hpp"

OSDefineMetaClassAndStructors(VirtioVideoFramebuffer, IOFramebuffer)

namespace {
// 이 boot-arg가 있어야 매칭한다 (VirtioGpuDeviceTakeover.cpp와 같은 값).
constexpr const char *kTakeoverBootArg = "vvideo_takeover";
// 하드웨어 커서를 끄고 소프트웨어 커서를 쓴다 (REQ-051, WI-4.001).
constexpr const char *kSoftwareCursorBootArg = "-vvideoswcursor";
// 창 크기 자동 전환 (WI-6.001). boot-arg가 있으면 그것을, 없으면 GPU personality의 VVAutoResize를 쓴다. 0이면 끈다.
constexpr const char *kAutoResizeBootArg = "vvideo_autoresize";
constexpr const char *kAutoResizeProperty = "VVAutoResize";
// 창을 끄는 동안 이벤트가 연달아 온다. 마지막 이벤트 뒤 이만큼 조용하면 연결 변경을 알린다.
constexpr uint32_t kResizeSettleMs = 600;
// 전원 상태 (IONDRVFramebuffer와 같은 3단계).
constexpr unsigned long kPowerStateCount = 3;
constexpr unsigned long kPowerStateIdle = 1;
constexpr unsigned long kPowerStateOn = 2;
// 깊이(색 형식)는 32bpp 하나뿐이다. 모드 ID는 vv/ModeTable.hpp가 정한다(1부터, 재부팅 간 고정).
constexpr IOIndex kOnlyDepth = 0;
// 60 Hz, 16.16 고정소수점.
constexpr IOFixed1616 kRefreshRate = 60 << 16;
constexpr UInt32 kBitsPerPixel = 32;
constexpr UInt32 kBitsPerComponent = 8;
constexpr UInt32 kComponentCount = 3;
// B,G,R,X 바이트 순서(little-endian 32비트 값 0x00RRGGBB). IO32BitDirectPixels와 같다.
constexpr UInt32 kRedMask = 0x00FF0000;
constexpr UInt32 kGreenMask = 0x0000FF00;
constexpr UInt32 kBlueMask = 0x000000FF;
// VVFbTrace 길이 제한.
constexpr uint32_t kMaxTraceEntries = 48;
constexpr size_t kTraceBytes = 2048;
constexpr size_t kTraceEntryBytes = 48;

// IOSelect(4문자 코드)를 읽을 수 있는 문자열로. 출력 문자가 아니면 '.'.
void selectorName(uint32_t selector, char out[5])
{
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>((selector >> (24 - 8 * i)) & 0xFF);
        out[i] = (c >= 0x20 && c < 0x7F) ? c : '.';
    }
    out[4] = '\0';
}

bool softwareCursorForced()
{
    int unused = 0;
    return PE_parse_boot_argn(kSoftwareCursorBootArg, &unused, sizeof(unused));
}

bool autoResizeEnabled(IOService *gpu)
{
    uint32_t value = 0;
    if (PE_parse_boot_argn(kAutoResizeBootArg, &value, sizeof(value))) {
        return value != 0;
    }
    auto *setting = OSDynamicCast(OSNumber, gpu->getProperty(kAutoResizeProperty));
    return setting != nullptr && setting->unsigned32BitValue() != 0;
}

bool takeoverEnabled()
{
    int value = 0;
    return PE_parse_boot_argn(kTakeoverBootArg, &value, sizeof(value)) && value != 0;
}
} // namespace

VirtioGpuDevice *VirtioVideoFramebuffer::findGpuDevice(IOService *pci)
{
    // VirtioGpuDevice는 같은 PCI 장치에 다른 카테고리(VirtioVideoGpu)로 붙어 있다.
    OSIterator *clients = pci->getClientIterator();
    if (clients == nullptr) {
        return nullptr;
    }
    VirtioGpuDevice *found = nullptr;
    while (OSObject *object = clients->getNextObject()) {
        if (auto *gpu = OSDynamicCast(VirtioGpuDevice, object)) {
            gpu->retain();
            found = gpu;
            break;
        }
    }
    clients->release();
    return found;
}

IOService *VirtioVideoFramebuffer::probe(IOService *provider, SInt32 *score)
{
    // 거절하면 객체가 바로 해제되므로 로그를 남길 곳이 없다.
    // 매칭 조건: PCI 장치에 AAPL,iokit-ignore-ndrv가 있거나(OpenCore DeviceProperties, IONDRV가 붙지 않음)
    // 실행 중 넘겨받기 boot-arg가 있을 때. 둘 다 없으면 IONDRV가 화면을 맡도록 빠진다.
    if (vv::isDriverDisabledByBootArg() ||
        (provider->getProperty(vv::kIgnoreNdrvProperty) == nullptr && !takeoverEnabled())) {
        return nullptr;
    }
    VirtioGpuDevice *gpu = findGpuDevice(provider);
    const bool ready = gpu != nullptr && gpu->isReadyForFramebuffer();
    if (gpu != nullptr) {
        gpu->release();
    }
    if (!ready) {
        return nullptr;
    }
    return IOFramebuffer::probe(provider, score);
}

void VirtioVideoFramebuffer::trace(const char *what, uint32_t selector, uint64_t value)
{
    if (mTraceEntries >= kMaxTraceEntries) {
        return;
    }
    ++mTraceEntries;
    char entry[kTraceEntryBytes];
    if (selector != 0) {
        char name[5];
        selectorName(selector, name);
        snprintf(entry, sizeof(entry), "%s(%s)=%llu", what, name, value);
    } else {
        snprintf(entry, sizeof(entry), "%s", what);
    }
    OSString *previous = OSDynamicCast(OSString, getProperty("VVFbTrace"));
    char joined[kTraceBytes];
    snprintf(joined, sizeof(joined), "%s%s%s", previous != nullptr ? previous->getCStringNoCopy() : "",
             previous != nullptr ? " " : "", entry);
    setProperty("VVFbTrace", joined);
}

void VirtioVideoFramebuffer::tracePower(const char *what, IOIndex connectIndex, uint32_t selector, uint64_t value)
{
    char name[5];
    selectorName(selector, name);
    snprintf(mPowerTrace[mPowerTraceCount % kPowerTraceEntries], kPowerTraceEntryBytes, "%s%d(%s)=%llu", what,
             static_cast<int>(connectIndex), name, value);
    ++mPowerTraceCount;
    // 오래된 것부터 공백으로 이어 붙인다. 앞에 총 개수를 둬서 빠진 항목이 있는지 알 수 있게 한다.
    char joined[kPowerTraceEntries * kPowerTraceEntryBytes + 16];
    int used = snprintf(joined, sizeof(joined), "n=%u", mPowerTraceCount);
    const uint32_t kept = mPowerTraceCount < kPowerTraceEntries ? mPowerTraceCount : kPowerTraceEntries;
    for (uint32_t i = 0; i < kept && used > 0 && static_cast<size_t>(used) < sizeof(joined); ++i) {
        const uint32_t slot = (mPowerTraceCount - kept + i) % kPowerTraceEntries;
        used += snprintf(joined + used, sizeof(joined) - used, " %s", mPowerTrace[slot]);
    }
    setProperty("VVFbPowerTrace", joined);
}

bool VirtioVideoFramebuffer::start(IOService *provider)
{
    mGpu = findGpuDevice(provider);
    if (mGpu == nullptr) {
        return false;
    }
    mBuffer = mGpu->copyFramebuffer(&mMode);
    if (mBuffer == nullptr) {
        mGpu->release();
        mGpu = nullptr;
        return false;
    }
    // WI-3.001·3.004: 표준 모드 + 장치 선호 해상도. 모두 같은 버퍼의 앞부분을 쓰므로 버퍼에 들어가는 것만 보고한다.
    mModes = vv::display::buildModeTable(mGpu->preferredMode(), mBuffer->length());
    const vv::display::ModeEntry *startup = vv::display::findPreferredMode(mModes);
    if (startup == nullptr || mGpu->changeMode(startup->mode) != kIOReturnSuccess) {
        return false;  // free()가 정리한다
    }
    mMode = startup->mode;
    mStartupModeId = mCurrentModeId = static_cast<IODisplayModeID>(startup->id);
    trace("start");
    if (!IOFramebuffer::start(provider)) {
        return false;  // free()가 정리한다
    }
    trace("start-done");
    mAutoResize = autoResizeEnabled(mGpu);
    // 프레임버퍼의 getWorkLoop()는 start 시점에 null이었다(0.0.43 실측). 이미 있는 GPU 서비스의 워크루프에 붙인다.
    IOWorkLoop *workLoop = mGpu->getWorkLoop();
    mResizeTimer = IOTimerEventSource::timerEventSource(this, &VirtioVideoFramebuffer::resizeTimerFired);
    if (mResizeTimer == nullptr || workLoop == nullptr || workLoop->addEventSource(mResizeTimer) != kIOReturnSuccess) {
        // 자동 전환만 못 할 뿐 화면 출력에는 지장이 없으므로 시작은 계속한다.
        VV_LOG_ERR("fb: resize timer unavailable, auto resize disabled");
        if (mResizeTimer != nullptr) {
            mResizeTimer->release();
            mResizeTimer = nullptr;
        }
        mAutoResize = false;
    }
    publishAutoResize();
    mGpu->framebufferAttached(this, true);
    VV_LOG_INFO("fb: started %ux%u", mMode.width, mMode.height);
    vv::publishLog(mGpu);
    return true;
}

void VirtioVideoFramebuffer::initForPM()
{
    if (mPMInitialized || getProvider() == nullptr) {
        return;
    }
    // IONDRVFramebuffer와 같은 3단계: 0 꺼짐, 1 대기(디스플레이 꺼짐·장치는 전원), 2 켜짐(사용 가능).
    static IOPMPowerState powerStates[kPowerStateCount] = {
        {kIOPMPowerStateVersion1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {kIOPMPowerStateVersion1, 0, 0, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0},
        {kIOPMPowerStateVersion1, kIOPMDeviceUsable | kIOPMMaxPerformance, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0,
         0, 0, 0},
    };
    PMinit();
    getProvider()->joinPMtree(this);
    const IOReturn registered = registerPowerDriver(this, powerStates, kPowerStateCount);
    // IONDRVFramebuffer::initForPM과 같게: 디스플레이(자식)가 붙을 때까지 clamp로 켜 두고, 자기 요구는 1(대기)로
    // 둔다. 그러면 켜짐(2)은 자식 IODisplay의 요구로만 유지되고 디스플레이 잠자기 때 1로 내려간다.
    // 0.0.31·0.0.32는 changePowerStateTo(2)로 고정해서 디스플레이 잠자기에도 상태가 내려가지 않았다 (WI-4.002).
    temporaryPowerClampOn();
    changePowerStateTo(kPowerStateIdle);
    mPMInitialized = true;
    trace("initForPM", 0, static_cast<uint32_t>(registered));
    VV_LOG_INFO("fb: power management registered -> 0x%x", registered);
}

void VirtioVideoFramebuffer::stop(IOService *provider)
{
    if (mPMInitialized) {
        PMstop();
        mPMInitialized = false;
    }
    if (mGpu != nullptr) {
        if (mScanoutStarted) {
            mGpu->stopScanout();
            mScanoutStarted = false;
        }
        mGpu->framebufferAttached(this, false);
    }
    if (mResizeTimer != nullptr) {
        mResizeTimer->cancelTimeout();
        if (IOWorkLoop *workLoop = mGpu != nullptr ? mGpu->getWorkLoop() : nullptr; workLoop != nullptr) {
            workLoop->removeEventSource(mResizeTimer);
        }
        mResizeTimer->release();
        mResizeTimer = nullptr;
    }
    mConnectProc = nullptr;
    IOFramebuffer::stop(provider);
}

void VirtioVideoFramebuffer::free()
{
    if (mBuffer != nullptr) {
        mBuffer->release();
        mBuffer = nullptr;
    }
    if (mGpu != nullptr) {
        mGpu->release();
        mGpu = nullptr;
    }
    IOFramebuffer::free();
}

IOReturn VirtioVideoFramebuffer::enableController()
{
    if (mGpu == nullptr) {
        return kIOReturnNotReady;
    }
    const IOReturn result = mGpu->startScanout();
    mScanoutStarted = result == kIOReturnSuccess;
    initForPM();
    trace("enableController", 0, static_cast<uint32_t>(result));
    VV_LOG_INFO("fb: enableController -> 0x%x", result);
    vv::publishLog(mGpu);
    return result;
}

bool VirtioVideoFramebuffer::isConsoleDevice()
{
    // 부트 콘솔은 IONDRV(펌웨어 GOP)가 맡았다. 넘겨받은 뒤 콘솔 처리는 P2 후반(WI-2.009)에서 다룬다.
    return false;
}

IODeviceMemory *VirtioVideoFramebuffer::makeApertureRange() const
{
    // T-01: 프레임버퍼는 물리 연속이고 장치 주소 = 물리 주소다. 그 물리 범위를 그대로 aperture로 준다.
    if (mBuffer == nullptr || mBuffer->physicalAddress() == 0) {
        return nullptr;
    }
    return IODeviceMemory::withRange(static_cast<IOPhysicalAddress>(mBuffer->physicalAddress()),
                                     static_cast<IOPhysicalLength>(mBuffer->length()));
}

IODeviceMemory *VirtioVideoFramebuffer::getApertureRange(IOPixelAperture aperture)
{
    if (aperture != kIOFBSystemAperture) {
        return nullptr;
    }
    return makeApertureRange();
}

IODeviceMemory *VirtioVideoFramebuffer::getVRAMRange()
{
    // VRAM = 프레임버퍼 메모리 풀 전체 (WI-3.008, REQ-060). 풀을 128 MiB로 잡으면 그대로 IOFBMemorySize가 된다.
    return makeApertureRange();
}

const char *VirtioVideoFramebuffer::getPixelFormats()
{
    // 헤더 규약: 지원하는 형식 문자열들을 이어 붙이고 빈 문자열로 끝낸다.
    static const char kPixelFormats[] = IO32BitDirectPixels "\0";
    return kPixelFormats;
}

IOItemCount VirtioVideoFramebuffer::getDisplayModeCount()
{
    return static_cast<IOItemCount>(mModes.count);
}

IOReturn VirtioVideoFramebuffer::getDisplayModes(IODisplayModeID *allDisplayModes)
{
    if (allDisplayModes == nullptr) {
        return kIOReturnBadArgument;
    }
    for (size_t i = 0; i < mModes.count; ++i) {
        allDisplayModes[i] = static_cast<IODisplayModeID>(mModes.entries[i].id);
    }
    return kIOReturnSuccess;
}

IOReturn VirtioVideoFramebuffer::getInformationForDisplayMode(IODisplayModeID displayMode,
                                                              IODisplayModeInformation *info)
{
    const vv::display::ModeEntry *entry = vv::display::findModeById(mModes, static_cast<uint32_t>(displayMode));
    if (info == nullptr || entry == nullptr) {
        return kIOReturnBadArgument;
    }
    bzero(info, sizeof(*info));
    info->nominalWidth = entry->mode.width;
    info->nominalHeight = entry->mode.height;
    info->refreshRate = kRefreshRate;
    info->maxDepthIndex = kOnlyDepth;
    info->flags = kDisplayModeValidFlag | kDisplayModeSafeFlag | (entry->preferred ? kDisplayModeDefaultFlag : 0);
    return kIOReturnSuccess;
}

UInt64 VirtioVideoFramebuffer::getPixelFormatsForDisplayMode(IODisplayModeID, IOIndex)
{
    // 헤더 설명: obsolete, 하위 클래스는 0을 돌려줘야 한다.
    return 0;
}

IOReturn VirtioVideoFramebuffer::getPixelInformation(IODisplayModeID displayMode, IOIndex depth,
                                                     IOPixelAperture aperture, IOPixelInformation *pixelInfo)
{
    const vv::display::ModeEntry *entry = vv::display::findModeById(mModes, static_cast<uint32_t>(displayMode));
    if (pixelInfo == nullptr || entry == nullptr || depth != kOnlyDepth || aperture != kIOFBSystemAperture) {
        return kIOReturnBadArgument;
    }
    const vv::display::Mode mode = entry->mode;
    bzero(pixelInfo, sizeof(*pixelInfo));
    pixelInfo->bytesPerRow = vv::display::rowBytes(mode);
    pixelInfo->bytesPerPlane = 0;
    pixelInfo->bitsPerPixel = kBitsPerPixel;
    pixelInfo->pixelType = kIORGBDirectPixels;
    pixelInfo->componentCount = kComponentCount;
    pixelInfo->bitsPerComponent = kBitsPerComponent;
    pixelInfo->componentMasks[0] = kRedMask;
    pixelInfo->componentMasks[1] = kGreenMask;
    pixelInfo->componentMasks[2] = kBlueMask;
    strlcpy(pixelInfo->pixelFormat, IO32BitDirectPixels, sizeof(pixelInfo->pixelFormat));
    pixelInfo->activeWidth = mode.width;
    pixelInfo->activeHeight = mode.height;
    return kIOReturnSuccess;
}

IOReturn VirtioVideoFramebuffer::getCurrentDisplayMode(IODisplayModeID *displayMode, IOIndex *depth)
{
    if (displayMode != nullptr) {
        *displayMode = mCurrentModeId;
    }
    if (depth != nullptr) {
        *depth = kOnlyDepth;
    }
    return kIOReturnSuccess;
}

IOReturn VirtioVideoFramebuffer::setDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
    trace("setDisplayMode", 0, static_cast<uint64_t>(displayMode));
    // WI-3.002: 같은 버퍼 안에서 새 크기의 resource로 바꾼다 (VirtioGpuDevice::changeMode).
    const vv::display::ModeEntry *entry = vv::display::findModeById(mModes, static_cast<uint32_t>(displayMode));
    if (entry == nullptr || depth != kOnlyDepth || mGpu == nullptr) {
        return kIOReturnUnsupported;
    }
    const IOReturn result = mGpu->changeMode(entry->mode);
    if (result == kIOReturnSuccess) {
        mMode = entry->mode;
        mCurrentModeId = displayMode;
    }
    VV_LOG_INFO("fb: setDisplayMode %d (%ux%u) -> 0x%x", static_cast<int>(displayMode), entry->mode.width,
                entry->mode.height, result);
    vv::publishLog(mGpu);
    return result;
}

IOReturn VirtioVideoFramebuffer::getStartupDisplayMode(IODisplayModeID *displayMode, IOIndex *depth)
{
    // 장치 선호 해상도. 마지막 선택 모드 복원(WI-3.007)은 macOS가 저장한 값을 쓰는지 먼저 실측한다.
    if (displayMode != nullptr) {
        *displayMode = mStartupModeId;
    }
    if (depth != nullptr) {
        *depth = kOnlyDepth;
    }
    return kIOReturnSuccess;
}

IOReturn VirtioVideoFramebuffer::setGammaTable(UInt32, UInt32, UInt32, void *)
{
    // virtio-gpu 2D에는 감마 명령이 없다. 받기만 하고 성공을 돌려준다 (REQ-052).
    return kIOReturnSuccess;
}

IOReturn VirtioVideoFramebuffer::getAttribute(IOSelect attribute, uintptr_t *value)
{
    trace("get", attribute);
    if (attribute == kIOPowerAttribute) {
        if (value != nullptr) {
            *value = mPowerState;
        }
        return kIOReturnSuccess;
    }
    if (attribute == kIOHardwareCursorAttribute) {
        // 하드웨어 커서 (WI-4.001, REQ-033). boot-arg로 끄면 미지원을 보고해 IOGraphics가 소프트웨어 커서를
        // 프레임버퍼에 그리게 한다 (REQ-051).
        if (value != nullptr) {
            *value = (mGpu != nullptr && !softwareCursorForced()) ? 1 : 0;
        }
        return kIOReturnSuccess;
    }
    return IOFramebuffer::getAttribute(attribute, value);
}

IOReturn VirtioVideoFramebuffer::setAttribute(IOSelect attribute, uintptr_t value)
{
    trace("set", attribute, value);
    if (attribute == kIOPowerAttribute || attribute == kIOPowerStateAttribute) {
        tracePower("set", -1, attribute, value);
    }
    if (attribute == kIOPowerStateAttribute && mGpu != nullptr) {
        // 프레임버퍼 전원 단계(0·1·2). 2(사용 가능)보다 낮으면 디스플레이가 꺼진 것이다.
        mGpu->noteFramebufferPowerState(value);
        mGpu->setDisplayPowered(value >= kPowerStateOn);
    }
    if (attribute == kIOPowerAttribute) {
        // 값만 기억한다. 켜고 끄는 판단은 위의 'pwrs'(전원 단계)로만 한다: 디스플레이 잠자기 때 IOFramebuffer는
        // pwrs=1 다음에 powr=1을 보내므로, powr≠0을 켜짐으로 보면 끈 직후 다시 켜 버린다 (0.0.35 실측, WI-4.002).
        mPowerState = value;
        return kIOReturnSuccess;
    }
    return IOFramebuffer::setAttribute(attribute, value);
}

bool VirtioVideoFramebuffer::hasDDCConnect(IOIndex connectIndex)
{
    uint8_t edid[vv::virtio::gpu::kMaxEdidBytes];
    return connectIndex == 0 && mGpu != nullptr && mGpu->copyEdid(edid, sizeof(edid)) > 0;
}

IOReturn VirtioVideoFramebuffer::getDDCBlock(IOIndex connectIndex, UInt32 blockNumber, IOSelect blockType,
                                             IOOptionBits, UInt8 *data, IOByteCount *length)
{
    trace("getDDCBlock", 0, blockNumber);
    if (connectIndex != 0 || blockType != kIODDCBlockTypeEDID || data == nullptr || length == nullptr ||
        blockNumber == 0 || mGpu == nullptr) {
        return kIOReturnBadArgument;
    }
    uint8_t edid[vv::virtio::gpu::kMaxEdidBytes];
    const size_t size = mGpu->copyEdid(edid, sizeof(edid));
    const size_t offset = (blockNumber - 1) * vv::edid::kBlockBytes;  // blockNumber는 1부터
    if (size == 0 || offset + vv::edid::kBlockBytes > size || *length < vv::edid::kBlockBytes) {
        return kIOReturnUnsupported;
    }
    bcopy(edid + offset, data, vv::edid::kBlockBytes);
    *length = vv::edid::kBlockBytes;
    return kIOReturnSuccess;
}

void VirtioVideoFramebuffer::displayConfigChanged()
{
    if (mGpu == nullptr || mBuffer == nullptr) {
        return;
    }
    // 지금 쓰는 모드가 동적 모드(창 크기)고 새 크기가 표준 모드면, 지금 화면이 목록에서 빠지지 않게 남긴다 (WI-6.001).
    const vv::display::ModeEntry *currentEntry = vv::display::findModeById(mModes, static_cast<uint32_t>(mCurrentModeId));
    const vv::display::ModeEntry keep = currentEntry != nullptr ? *currentEntry : vv::display::ModeEntry{};
    const vv::display::ModeTable updated =
        vv::display::buildModeTable(mGpu->preferredMode(), mBuffer->length(), currentEntry != nullptr ? &keep : nullptr);
    const vv::display::ModeEntry *preferred = vv::display::findPreferredMode(updated);
    // 현재 모드가 새 목록에 없거나 크기가 달라지면, 현재 화면과 맞지 않는 목록을 보고하게 된다. 이때는 목록을 바꾸지 않는다.
    // 예외: 자동 전환이 켜져 있고 현재 모드가 동적 모드면 크기가 바뀌어도 바꾼다. 곧 연결 변경을 알려 IOGraphics가 목록을
    // 다시 읽고 같은 ID로 setDisplayMode를 부른다(0.0.46 실측: 연결 변경 직후 EDID 기본 해상도 모드로 전환).
    const vv::display::ModeEntry *current = vv::display::findModeById(updated, static_cast<uint32_t>(mCurrentModeId));
    const bool dynamicResize = mAutoResize && mResizeTimer != nullptr && current != nullptr &&
                               vv::display::isDynamicModeId(current->id);
    if (current == nullptr || (!(current->mode == mMode) && !dynamicResize)) {
        VV_LOG_INFO("fb: display changed, preferred %ux%u; current mode %d not in new list, keeping list",
                    preferred != nullptr ? preferred->mode.width : 0, preferred != nullptr ? preferred->mode.height : 0,
                    static_cast<int>(mCurrentModeId));
        trace("displayChanged-kept");
        return;
    }
    mModes = updated;
    if (preferred != nullptr) {
        mStartupModeId = static_cast<IODisplayModeID>(preferred->id);
    }
    VV_LOG_INFO("fb: display changed, mode list rebuilt (%u modes, preferred %ux%u)", static_cast<unsigned>(mModes.count),
                preferred != nullptr ? preferred->mode.width : 0, preferred != nullptr ? preferred->mode.height : 0);
    trace("displayChanged-rebuilt");
    // 게스트가 모드를 바꾸면 QEMU가 창을 그 크기로 맞추고 같은 크기로 이벤트를 보낸다(0.0.42 실측). 그때는 알릴 것이 없다.
    if (preferred == nullptr || preferred->mode == mMode) {
        return;
    }
    if (!mAutoResize || mResizeTimer == nullptr) {
        VV_LOG_INFO("fb: auto resize off, not announcing %ux%u", preferred->mode.width, preferred->mode.height);
        return;
    }
    mResizeTarget = preferred->mode;
    mResizeTimer->setTimeoutMS(kResizeSettleMs);  // 이벤트가 더 오면 다시 기다린다
}

void VirtioVideoFramebuffer::resizeTimerFired(OSObject *owner, IOTimerEventSource *)
{
    auto *self = OSDynamicCast(VirtioVideoFramebuffer, owner);
    if (self == nullptr || self->mGpu == nullptr) {
        return;
    }
    if (self->mConnectProc == nullptr || !self->mConnectEnabled) {
        VV_LOG_INFO("fb: no connect-change handler registered, cannot announce %ux%u", self->mResizeTarget.width,
                    self->mResizeTarget.height);
    } else {
        ++self->mConnectChanges;
        VV_LOG_INFO("fb: connect change for window size %ux%u", self->mResizeTarget.width, self->mResizeTarget.height);
        self->trace("connectChange");
        self->mConnectProc(self->mConnectTarget, self->mConnectRef);
    }
    self->publishAutoResize();
    vv::publishLog(self->mGpu);
}

void VirtioVideoFramebuffer::publishAutoResize()
{
    if (mGpu == nullptr) {
        return;
    }
    char summary[96];
    snprintf(summary, sizeof(summary), "%s handler=%s connect-changes=%u last=%ux%u", mAutoResize ? "on" : "off",
             mConnectProc == nullptr ? "none" : (mConnectEnabled ? "enabled" : "disabled"), mConnectChanges,
             mResizeTarget.width, mResizeTarget.height);
    mGpu->setProperty("VVAutoResizeState", summary);
}

IOReturn VirtioVideoFramebuffer::registerForInterruptType(IOSelect interruptType, IOFBInterruptProc proc,
                                                          OSObject *target, void *ref, void **interruptRef)
{
    trace("regInt", interruptType);
    if (interruptType != kIOFBConnectInterruptType) {
        return IOFramebuffer::registerForInterruptType(interruptType, proc, target, ref, interruptRef);
    }
    if (proc == nullptr || interruptRef == nullptr) {
        return kIOReturnBadArgument;
    }
    mConnectTarget = target;
    mConnectRef = ref;
    mConnectProc = proc;
    mConnectEnabled = true;
    *interruptRef = &mConnectProc;  // 이 등록을 가리키는 표식. unregister·setInterruptState에서 비교한다
    publishAutoResize();
    return kIOReturnSuccess;
}

IOReturn VirtioVideoFramebuffer::unregisterInterrupt(void *interruptRef)
{
    if (interruptRef != &mConnectProc) {
        return IOFramebuffer::unregisterInterrupt(interruptRef);
    }
    mConnectProc = nullptr;
    mConnectEnabled = false;
    publishAutoResize();
    return kIOReturnSuccess;
}

IOReturn VirtioVideoFramebuffer::setInterruptState(void *interruptRef, UInt32 state)
{
    if (interruptRef != &mConnectProc) {
        return IOFramebuffer::setInterruptState(interruptRef, state);
    }
    mConnectEnabled = state != 0;
    return kIOReturnSuccess;
}

IOReturn VirtioVideoFramebuffer::setCursorImage(void *cursorImage)
{
    if (mGpu == nullptr || softwareCursorForced()) {
        return kIOReturnUnsupported;
    }
    uint8_t *pixels = mGpu->prepareCursorImage();
    if (pixels == nullptr) {
        mGpu->hideCursor();
        return kIOReturnUnsupported;
    }
    // 장치 커서 형식(64×64, 32비트 ARGB)을 알려 주고 IOGraphics가 변환하게 한다 (헤더 규약: 실패하면 숨기고
    // kIOReturnUnsupported).
    IOHardwareCursorDescriptor description{};
    description.majorVersion = kHardwareCursorDescriptorMajorVersion;
    description.minorVersion = kHardwareCursorDescriptorMinorVersion;
    description.width = vv::cursor::kSize;
    description.height = vv::cursor::kSize;
    description.bitDepth = kIO32ARGBPixelFormat;
    IOHardwareCursorInfo info{};
    info.majorVersion = kHardwareCursorInfoMajorVersion;
    info.minorVersion = kHardwareCursorInfoMinorVersion;
    info.hardwareCursorData = pixels;
    if (!convertCursorImage(cursorImage, &description, &info)) {
        mGpu->hideCursor();
        return kIOReturnUnsupported;
    }
    return mGpu->commitCursorImage(info.cursorWidth, info.cursorHeight, info.cursorHotSpotX, info.cursorHotSpotY);
}

IOReturn VirtioVideoFramebuffer::setCursorState(SInt32 x, SInt32 y, bool visible)
{
    if (mGpu == nullptr) {
        return kIOReturnNotReady;
    }
    mGpu->setCursorState(x, y, visible);
    return kIOReturnSuccess;
}

IOReturn VirtioVideoFramebuffer::setAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
                                                           uintptr_t value)
{
    tracePower("setConn", connectIndex, attribute, value);
    return IOFramebuffer::setAttributeForConnection(connectIndex, attribute, value);
}

IOReturn VirtioVideoFramebuffer::getAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
                                                           uintptr_t *value)
{
    trace("getConn", attribute);
    if (connectIndex == 0 && attribute == kConnectionSupportsHLDDCSense) {
        // 헤더 규약: hasDDCConnect·getDDCBlock을 지원하면 값 없이 성공을 돌려준다.
        return hasDDCConnect(connectIndex) ? kIOReturnSuccess : kIOReturnUnsupported;
    }
    // 연결(모니터)은 하나이고 항상 켜져 있다. EDID(DDC)는 WI-3.003.
    if (connectIndex == 0 && (attribute == kConnectionEnable || attribute == kConnectionCheckEnable)) {
        if (value != nullptr) {
            *value = 1;
        }
        return kIOReturnSuccess;
    }
    return IOFramebuffer::getAttributeForConnection(connectIndex, attribute, value);
}
