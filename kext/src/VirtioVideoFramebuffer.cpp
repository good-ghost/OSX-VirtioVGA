#include "VirtioVideoFramebuffer.hpp"

#include <IOKit/IODeviceMemory.h>
#include <pexpert/pexpert.h>

#include "VVBootArgs.hpp"
#include "VVDmaBuffer.hpp"
#include "VVLog.hpp"
#include "VirtioGpuDevice.hpp"

OSDefineMetaClassAndStructors(VirtioVideoFramebuffer, IOFramebuffer)

namespace {
// 이 boot-arg가 있어야 매칭한다 (VirtioGpuDeviceTakeover.cpp와 같은 값).
constexpr const char *kTakeoverBootArg = "vvideo_takeover";
// 모드는 하나뿐이다 (P3에서 목록으로 바꾼다). 0은 IOGraphics에서 "모드 없음"으로 쓰일 수 있어 1부터 쓴다.
constexpr IODisplayModeID kOnlyModeId = 1;
constexpr IOIndex kOnlyDepth = 0;
constexpr IOItemCount kModeCount = 1;
// 60 Hz, 16.16 고정소수점.
constexpr IOFixed1616 kRefreshRate = 60 << 16;
constexpr UInt32 kBitsPerPixel = 32;
constexpr UInt32 kBitsPerComponent = 8;
constexpr UInt32 kComponentCount = 3;
// B,G,R,X 바이트 순서(little-endian 32비트 값 0x00RRGGBB). IO32BitDirectPixels와 같다.
constexpr UInt32 kRedMask = 0x00FF0000;
constexpr UInt32 kGreenMask = 0x0000FF00;
constexpr UInt32 kBlueMask = 0x000000FF;

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
    // 거절하면 객체가 바로 해제되므로 로그를 남길 곳이 없다. 결과는 GPU 서비스의 VVTakeover로 본다.
    if (vv::isDriverDisabledByBootArg() || !takeoverEnabled()) {
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
    if (!IOFramebuffer::start(provider)) {
        return false;  // free()가 정리한다
    }
    mGpu->framebufferAttached(this, true);
    VV_LOG_INFO("fb: started %ux%u", mMode.width, mMode.height);
    vv::publishLog(mGpu);
    return true;
}

void VirtioVideoFramebuffer::stop(IOService *provider)
{
    if (mGpu != nullptr) {
        if (mScanoutStarted) {
            mGpu->stopScanout();
            mScanoutStarted = false;
        }
        mGpu->framebufferAttached(this, false);
    }
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
    // VRAM 128 MiB 보고(REQ-060)는 P3(WI-3.008). 지금은 프레임버퍼 크기.
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
    return kModeCount;
}

IOReturn VirtioVideoFramebuffer::getDisplayModes(IODisplayModeID *allDisplayModes)
{
    if (allDisplayModes == nullptr) {
        return kIOReturnBadArgument;
    }
    allDisplayModes[0] = kOnlyModeId;
    return kIOReturnSuccess;
}

IOReturn VirtioVideoFramebuffer::getInformationForDisplayMode(IODisplayModeID displayMode,
                                                              IODisplayModeInformation *info)
{
    if (info == nullptr || displayMode != kOnlyModeId) {
        return kIOReturnBadArgument;
    }
    bzero(info, sizeof(*info));
    info->nominalWidth = mMode.width;
    info->nominalHeight = mMode.height;
    info->refreshRate = kRefreshRate;
    info->maxDepthIndex = kOnlyDepth;
    info->flags = kDisplayModeValidFlag | kDisplayModeSafeFlag | kDisplayModeDefaultFlag;
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
    if (pixelInfo == nullptr || displayMode != kOnlyModeId || depth != kOnlyDepth || aperture != kIOFBSystemAperture) {
        return kIOReturnBadArgument;
    }
    bzero(pixelInfo, sizeof(*pixelInfo));
    pixelInfo->bytesPerRow = vv::display::rowBytes(mMode);
    pixelInfo->bytesPerPlane = 0;
    pixelInfo->bitsPerPixel = kBitsPerPixel;
    pixelInfo->pixelType = kIORGBDirectPixels;
    pixelInfo->componentCount = kComponentCount;
    pixelInfo->bitsPerComponent = kBitsPerComponent;
    pixelInfo->componentMasks[0] = kRedMask;
    pixelInfo->componentMasks[1] = kGreenMask;
    pixelInfo->componentMasks[2] = kBlueMask;
    strlcpy(pixelInfo->pixelFormat, IO32BitDirectPixels, sizeof(pixelInfo->pixelFormat));
    pixelInfo->activeWidth = mMode.width;
    pixelInfo->activeHeight = mMode.height;
    return kIOReturnSuccess;
}

IOReturn VirtioVideoFramebuffer::getCurrentDisplayMode(IODisplayModeID *displayMode, IOIndex *depth)
{
    if (displayMode != nullptr) {
        *displayMode = kOnlyModeId;
    }
    if (depth != nullptr) {
        *depth = kOnlyDepth;
    }
    return kIOReturnSuccess;
}

IOReturn VirtioVideoFramebuffer::setDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
    // 모드 전환은 WI-3.002. 지금은 하나뿐인 모드만 받는다.
    return displayMode == kOnlyModeId && depth == kOnlyDepth ? kIOReturnSuccess : kIOReturnUnsupported;
}

IOReturn VirtioVideoFramebuffer::getStartupDisplayMode(IODisplayModeID *displayMode, IOIndex *depth)
{
    return getCurrentDisplayMode(displayMode, depth);
}

IOReturn VirtioVideoFramebuffer::setGammaTable(UInt32, UInt32, UInt32, void *)
{
    // virtio-gpu 2D에는 감마 명령이 없다. 받기만 하고 성공을 돌려준다 (REQ-052).
    return kIOReturnSuccess;
}

IOReturn VirtioVideoFramebuffer::getAttribute(IOSelect attribute, uintptr_t *value)
{
    if (attribute == kIOHardwareCursorAttribute) {
        // 하드웨어 커서는 WI-4.001. 지금은 미지원을 보고해 소프트웨어 커서를 쓰게 한다 (REQ-051).
        if (value != nullptr) {
            *value = 0;
        }
        return kIOReturnSuccess;
    }
    return IOFramebuffer::getAttribute(attribute, value);
}

IOReturn VirtioVideoFramebuffer::getAttributeForConnection(IOIndex connectIndex, IOSelect attribute,
                                                           uintptr_t *value)
{
    // 연결(모니터)은 하나이고 항상 켜져 있다. EDID(DDC)는 WI-3.003.
    if (connectIndex == 0 && (attribute == kConnectionEnable || attribute == kConnectionCheckEnable)) {
        if (value != nullptr) {
            *value = 1;
        }
        return kIOReturnSuccess;
    }
    return IOFramebuffer::getAttributeForConnection(connectIndex, attribute, value);
}
