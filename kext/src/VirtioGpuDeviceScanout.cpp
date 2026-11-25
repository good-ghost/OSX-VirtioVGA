// 화면 출력 (WI-2.005, REQ-021, REQ-031, REQ-032).
//
// 프레임버퍼 클래스가 enableController에서 startScanout()을 부르면:
//   RESOURCE_CREATE_2D → ATTACH_BACKING(물리 연속, 항목 1개) → TRANSFER_TO_HOST_2D → SET_SCANOUT → RESOURCE_FLUSH
// 그 뒤 thread_call 하나가 kFlushIntervalMs마다 전체 화면을 TRANSFER + FLUSH 한다. IOFramebuffer에는 변경 영역
// 통지가 없어서 주기 전송을 쓴다 (REQ-032). 변경 감지(REQ-033)는 WI-4.004.
//
// 전송이 실패하면(장치 reset 등으로 resource를 잃은 경우) resource를 버리고 kResourceRetryMs 간격으로 다시 만든다.
// 명령은 워크루프 밖에서만 보낼 수 있어(sendCommand) 전송 루프를 thread_call에서 돌린다.
#include "VirtioGpuDevice.hpp"
#include "VirtioGpuDevicePrivate.hpp"

#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/OSAtomic.h>
#include <pexpert/pexpert.h>

#include "VVDmaBuffer.hpp"
#include "VirtioPciTransport.hpp"
#include "VVLog.hpp"

namespace {
namespace v = vv::virtio;
using namespace vv::gpudevice;

// resource를 잃었을 때 다시 만드는 간격.
constexpr uint32_t kResourceRetryMs = 1000;
// 디스플레이가 꺼져 있을 때 깨어났는지 확인하는 간격. 깨어난 뒤 첫 화면이 늦어도 이 정도다.
constexpr uint32_t kPausedPollMs = 50;
// 속성 갱신 간격. 매 전송마다 setProperty를 부르지 않도록 약 1초에 한 번만 갱신한다.
constexpr uint32_t kPublishIntervalMs = 1000;
// 부트 화면 복사 조건: 펌웨어 화면이 32bpp일 때만 그대로 복사할 수 있다.
constexpr unsigned long kBootScreenDepth = 32;
constexpr uint32_t kScanoutId = 0;
// stopScanout에서 전송 루프가 끝나기를 기다리는 한도.
constexpr uint32_t kFlushLoopDrainMs = 3000;
constexpr uint32_t kDrainPollMs = 1;
} // namespace

bool VirtioGpuDevice::isReadyForFramebuffer() const
{
    return !mStopping && !mGivenUp && mTransport != nullptr && mFramebuffer != nullptr &&
           mFramebuffer->physicalAddress() != 0 && vv::display::isUsable(mFramebufferMode);
}

VVDmaBuffer *VirtioGpuDevice::copyFramebuffer(vv::display::Mode *modeOut) const
{
    if (!isReadyForFramebuffer()) {
        return nullptr;
    }
    mFramebuffer->retain();
    if (modeOut != nullptr) {
        *modeOut = mFramebufferMode;
    }
    return mFramebuffer;
}

void VirtioGpuDevice::framebufferAttached(IOService *framebuffer, bool attached)
{
    mFramebufferClient = attached ? framebuffer : nullptr;
    setProperty("VVFramebufferClient", attached);
    VV_LOG_INFO("gpu: framebuffer client %s", attached ? "attached" : "detached");
    vv::publishLog(this);
}

void VirtioGpuDevice::publishScanout(const char *state)
{
    char summary[kSummaryBytes];
    snprintf(summary, sizeof(summary), "%s resource=%u setups=%u flushes=%u flush-errors=%u blanks=%u", state,
             mScanoutResourceId, mScanoutSetups, mFlushes, mFlushErrors, mScanoutBlanks);
    setProperty("VVScanout", summary);
}

bool VirtioGpuDevice::setUpScanoutResource()
{
    const vv::display::Mode mode = mFramebufferMode;
    const uint32_t resourceId = allocateResourceId();
    if (createResource2d(resourceId, v::gpu::Format::B8G8R8X8Unorm, mode) != kIOReturnSuccess) {
        return false;
    }
    if (attachBacking(resourceId, mFramebuffer->deviceAddress(), static_cast<uint32_t>(mFramebuffer->length())) !=
            kIOReturnSuccess ||
        transferToHost(resourceId, mode) != kIOReturnSuccess || setScanout(kScanoutId, resourceId, mode) != kIOReturnSuccess ||
        flushResource(resourceId, mode) != kIOReturnSuccess) {
        // 만든 것은 정리한다. 장치가 이미 reset됐으면 이 명령도 실패하지만 무시해도 된다 (장치 쪽 상태가 이미 없다).
        detachBacking(resourceId);
        unrefResource(resourceId);
        return false;
    }
    mScanoutResourceId = resourceId;
    mScanoutBlanked = false;  // 방금 SET_SCANOUT으로 연결했다
    mShadowValid = false;     // 새 resource: 변경 감지 기준을 다시 잡는다 (WI-4.004)
    ++mScanoutSetups;
    VV_LOG_INFO("gpu: scanout %u -> resource %u (%ux%u)", kScanoutId, resourceId, mode.width, mode.height);
    return true;
}

void VirtioGpuDevice::tearDownScanoutResource(bool disableScanout)
{
    if (mScanoutResourceId == 0) {
        return;
    }
    if (disableScanout) {
        setScanout(kScanoutId, 0, mFramebufferMode);
    }
    detachBacking(mScanoutResourceId);
    unrefResource(mScanoutResourceId);
    mScanoutResourceId = 0;
}

void VirtioGpuDevice::copyBootScreen()
{
    // 펌웨어(OVMF GOP)가 그린 화면이 우리 프레임버퍼와 같은 크기·형식일 때만 복사한다. 아니면 검은 화면에서 시작한다.
    PE_Video boot{};
    const char *result = "skipped:no-console-info";
    IOPlatformExpert *platform = getPlatform();
    if (platform != nullptr && platform->getConsoleInfo(&boot) == kIOReturnSuccess && boot.v_baseAddr != 0) {
        const vv::display::Mode mode = mFramebufferMode;
        const uint32_t rowBytes = vv::display::rowBytes(mode);
        if (boot.v_width != mode.width || boot.v_height != mode.height || boot.v_depth != kBootScreenDepth ||
            boot.v_rowBytes < rowBytes) {
            result = "skipped:mode-mismatch";
        } else {
            const IOByteCount length = boot.v_rowBytes * boot.v_height;
            IOMemoryDescriptor *source =
                IOMemoryDescriptor::withPhysicalAddress(static_cast<IOPhysicalAddress>(boot.v_baseAddr), length, kIODirectionIn);
            IOMemoryMap *map = source != nullptr ? source->map() : nullptr;
            if (map == nullptr) {
                result = "failed:map";
            } else {
                const auto *src = reinterpret_cast<const uint8_t *>(map->getVirtualAddress());
                auto *dst = static_cast<uint8_t *>(mFramebuffer->cpuAddress());
                for (uint32_t y = 0; y < mode.height; ++y) {
                    bcopy(src + y * boot.v_rowBytes, dst + y * rowBytes, rowBytes);
                }
                result = "copied";
                map->release();
            }
            if (source != nullptr) {
                source->release();
            }
        }
    }
    char summary[kSummaryBytes];
    snprintf(summary, sizeof(summary), "%s boot=%lux%lu@%lu rowBytes=%lu", result, boot.v_width, boot.v_height,
             boot.v_depth, boot.v_rowBytes);
    setProperty("VVBootScreenCopy", summary);
    VV_LOG_INFO("gpu: boot screen %s", summary);
}

IOReturn VirtioGpuDevice::startScanout()
{
    if (!isReadyForFramebuffer() || mFlushCall == nullptr) {
        return kIOReturnNotReady;
    }
    if (mScanoutRequested) {
        return kIOReturnSuccess;
    }
    mScanoutRequested = true;
    if (!mBootScreenCopied) {
        copyBootScreen();
        mBootScreenCopied = true;
    }
    if (!setUpScanoutResource()) {
        // 루프가 kResourceRetryMs 간격으로 다시 시도한다.
        VV_LOG_ERR("gpu: initial scanout setup failed, will retry");
    }
    publishScanout("running");
    vv::publishLog(this);
    mFlushLoopRunning = true;
    thread_call_enter(mFlushCall);
    return kIOReturnSuccess;
}

void VirtioGpuDevice::stopScanout()
{
    if (!mScanoutRequested) {
        return;
    }
    mScanoutRequested = false;
    for (uint32_t waited = 0; mFlushLoopRunning && waited < kFlushLoopDrainMs; waited += kDrainPollMs) {
        IOSleep(kDrainPollMs);
    }
    if (!mStopping && mTransport != nullptr && !mGivenUp) {
        tearDownScanoutResource(true);
    }
    publishScanout("stopped");
    VV_LOG_INFO("gpu: scanout stopped");
    vv::publishLog(this);
}

void VirtioGpuDevice::noteFramebufferPowerState(unsigned long state)
{
    const uint16_t command = mPci != nullptr ? mPci->configRead16(kIOPCIConfigCommand) : 0;
    const uint8_t status = mTransport != nullptr ? mTransport->status() : 0;
    const bool queueOn = mTransport != nullptr && mTransport->isQueueEnabled(0);
    VV_LOG_INFO("gpu: fb power state %lu pci-cmd=0x%04x dev-status=0x%02x ctrlq=%d", state, command, status,
                queueOn ? 1 : 0);
    vv::publishLog(this);
}

void VirtioGpuDevice::setDisplayPowered(bool powered)
{
    if (powered == !mFlushPaused) {
        return;
    }
    mFlushPaused = !powered;
    if (powered) {
        ++mDisplayWakes;
    } else {
        ++mDisplaySleeps;
    }
    char summary[kSummaryBytes];
    snprintf(summary, sizeof(summary), "%s sleeps=%u wakes=%u", powered ? "on" : "off", mDisplaySleeps, mDisplayWakes);
    setProperty("VVDisplayPower", summary);
    VV_LOG_INFO("gpu: display power %s", summary);
    publishScanout(powered ? "running" : "paused");
    vv::publishLog(this);
}

IOReturn VirtioGpuDevice::changeMode(const vv::display::Mode &mode)
{
    if (!isReadyForFramebuffer()) {
        return kIOReturnNotReady;
    }
    if (!vv::display::isUsable(mode) || vv::display::framebufferBytes(mode) > mFramebuffer->length()) {
        return kIOReturnBadArgument;
    }
    if (mode == mFramebufferMode) {
        return kIOReturnSuccess;
    }
    const vv::display::Mode previous = mFramebufferMode;
    const bool wasRunning = mScanoutRequested;
    if (wasRunning) {
        // 전송 루프를 멈추고 기존 resource를 정리한다. scanout은 끄지 않는다(새 resource로 바로 바꾼다).
        mScanoutRequested = false;
        for (uint32_t waited = 0; mFlushLoopRunning && waited < kFlushLoopDrainMs; waited += kDrainPollMs) {
            IOSleep(kDrainPollMs);
        }
        tearDownScanoutResource(false);
    }
    mFramebufferMode = mode;
    // 새 크기만큼 검게 지운다 (이전 모드의 줄 간격으로 남은 내용이 어긋나 보이지 않게).
    bzero(mFramebuffer->cpuAddress(), static_cast<size_t>(vv::display::framebufferBytes(mode)));
    ++mModeChanges;
    char summary[kSummaryBytes];
    snprintf(summary, sizeof(summary), "%ux%u changes=%u", mode.width, mode.height, mModeChanges);
    setProperty("VVCurrentMode", summary);
    VV_LOG_INFO("gpu: mode %ux%u -> %ux%u", previous.width, previous.height, mode.width, mode.height);
    if (!wasRunning) {
        vv::publishLog(this);
        return kIOReturnSuccess;
    }
    mScanoutRequested = true;
    if (!setUpScanoutResource()) {
        VV_LOG_ERR("gpu: scanout setup after mode change failed, will retry");
    }
    publishScanout("running");
    vv::publishLog(this);
    mFlushLoopRunning = true;
    thread_call_enter(mFlushCall);
    return kIOReturnSuccess;
}

void VirtioGpuDevice::deferredFlushLoop(thread_call_param_t owner, thread_call_param_t)
{
    auto *self = static_cast<VirtioGpuDevice *>(owner);
    OSIncrementAtomic(&self->mDisplayQueryActive);
    self->runFlushLoop();
    self->mFlushLoopRunning = false;
    OSDecrementAtomic(&self->mDisplayQueryActive);
}

void VirtioGpuDevice::runFlushLoop()
{
    const uint32_t interval = mFlushIntervalMs;
    const uint32_t publishEvery = kPublishIntervalMs / interval > 0 ? kPublishIntervalMs / interval : 1;
    uint32_t sinceRetry = kResourceRetryMs;
    while (mScanoutRequested && !mStopping) {
        if (mGivenUp || mTransport == nullptr) {
            break;
        }
        if (mScanoutResourceId == 0) {
            // resource가 없으면(처음 실패했거나 장치 reset으로 잃음) 간격을 두고 다시 만든다.
            if (sinceRetry >= kResourceRetryMs) {
                sinceRetry = 0;
                if (setUpScanoutResource()) {
                    publishScanout("running");
                }
            }
            IOSleep(interval);
            sinceRetry += interval;
            continue;
        }
        const vv::display::Mode mode = mFramebufferMode;
        if (mFlushPaused) {
            // 디스플레이가 꺼져 있다 (WI-4.002). 전송을 멈추고 scanout을 끊는다(resource 0). virtio-gpu에는
            // 화면 끄기 명령이 없어서, 끊지 않으면 호스트 창에 마지막 화면이 그대로 남는다 (0.0.33 실측).
            if (!mScanoutBlanked) {
                const IOReturn blanked = setScanout(kScanoutId, 0, mode);
                mScanoutBlanked = blanked == kIOReturnSuccess;
                VV_LOG_INFO("gpu: scanout %u disabled for display sleep -> 0x%x", kScanoutId, blanked);
                if (mScanoutBlanked) {
                    ++mScanoutBlanks;
                    publishScanout("blanked");
                    vv::publishLog(this);
                } else {
                    ++mFlushErrors;
                    VV_LOG_ERR("gpu: scanout disable for display sleep failed");
                }
            }
            IOSleep(kPausedPollMs);
            continue;
        }
        if (mScanoutBlanked) {
            // 깨어났다. 같은 resource를 다시 연결하고, 아래에서 전체 화면을 보낸다.
            const IOReturn restored = setScanout(kScanoutId, mScanoutResourceId, mode);
            VV_LOG_INFO("gpu: scanout %u -> resource %u after display wake -> 0x%x", kScanoutId, mScanoutResourceId,
                        restored);
            if (restored == kIOReturnSuccess) {
                mScanoutBlanked = false;
                mShadowValid = false;  // 깨어난 뒤 첫 주기는 전체를 보낸다
                publishScanout("running");
                vv::publishLog(this);
            } else {
                ++mFlushErrors;
                VV_LOG_ERR("gpu: scanout re-enable after display sleep failed, recreating resource");
                tearDownScanoutResource(false);
                sinceRetry = kResourceRetryMs;
                continue;
            }
        }
        if (!flushFrame(mode)) {
            ++mFlushErrors;
            VV_LOG_ERR("gpu: flush failed, recreating resource");
            // 장치 쪽에 resource가 남아 있을 수 있으니 정리를 시도한 뒤 다시 만든다.
            tearDownScanoutResource(false);
            sinceRetry = kResourceRetryMs;
            publishScanout("retrying");
            vv::publishLog(this);
        } else {
            ++mFlushes;
            if (mFlushes % publishEvery == 0) {
                publishScanout("running");
                publishDirtyStats();
            }
        }
        IOSleep(interval);
    }
}
