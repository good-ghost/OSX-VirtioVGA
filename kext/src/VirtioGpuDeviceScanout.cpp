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

#include <libkern/OSAtomic.h>

#include "VVDmaBuffer.hpp"
#include "VVLog.hpp"

namespace {
namespace v = vv::virtio;
using namespace vv::gpudevice;

// 약 60 Hz (REQ-032 기본값).
constexpr uint32_t kFlushIntervalMs = 16;
// resource를 잃었을 때 다시 만드는 간격.
constexpr uint32_t kResourceRetryMs = 1000;
// 속성 갱신 간격(전송 횟수 기준). 매 전송마다 setProperty를 부르지 않기 위해서다 (약 1초).
constexpr uint32_t kPublishEveryFlushes = 60;
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
    snprintf(summary, sizeof(summary), "%s resource=%u setups=%u flushes=%u flush-errors=%u", state,
             mScanoutResourceId, mScanoutSetups, mFlushes, mFlushErrors);
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

IOReturn VirtioGpuDevice::startScanout()
{
    if (!isReadyForFramebuffer() || mFlushCall == nullptr) {
        return kIOReturnNotReady;
    }
    if (mScanoutRequested) {
        return kIOReturnSuccess;
    }
    mScanoutRequested = true;
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
            IOSleep(kFlushIntervalMs);
            sinceRetry += kFlushIntervalMs;
            continue;
        }
        const vv::display::Mode mode = mFramebufferMode;
        if (transferToHost(mScanoutResourceId, mode) != kIOReturnSuccess ||
            flushResource(mScanoutResourceId, mode) != kIOReturnSuccess) {
            ++mFlushErrors;
            VV_LOG_ERR("gpu: flush failed, recreating resource");
            // 장치 쪽에 resource가 남아 있을 수 있으니 정리를 시도한 뒤 다시 만든다.
            tearDownScanoutResource(false);
            sinceRetry = kResourceRetryMs;
            publishScanout("retrying");
            vv::publishLog(this);
        } else {
            ++mFlushes;
            if (mFlushes % kPublishEveryFlushes == 0) {
                publishScanout("running");
            }
        }
        IOSleep(kFlushIntervalMs);
    }
}
