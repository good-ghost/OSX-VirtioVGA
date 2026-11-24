// 시험 패턴 (WI-2.004, AC-04). boot-arg `vvideo_test_pattern=1`일 때만 돈다.
//
// 처음으로 화면을 virtio-gpu 출력으로 바꾸는 시험이라, 정해진 시간이 지나면 스스로 되돌린다:
//   1) 시작 kPatternStartDelayMs 뒤: 프레임버퍼에 빨강·초록·파랑·흰색 띠를 그리고
//      RESOURCE_CREATE_2D → ATTACH_BACKING → TRANSFER_TO_HOST_2D → SET_SCANOUT → RESOURCE_FLUSH
//   2) kPatternShowMs 동안 보여 준 뒤 SET_SCANOUT(resource 0)으로 scanout을 끈다
//   3) kPatternDisabledMs 뒤 DETACH_BACKING → RESOURCE_UNREF
//   4) 장치를 reset하고 다시 올린다 (QEMU가 VGA 출력으로 돌아가는지 보기 위해)
// 각 단계는 VVPatternTest 속성과 로그에 남는다. 화면이 각 단계에서 어떻게 보였는지는 사람이 확인한다.
#include "VirtioGpuDevice.hpp"
#include "VirtioGpuDevicePrivate.hpp"

#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>
#include <libkern/OSAtomic.h>

#include "VVDmaBuffer.hpp"
#include "VVLog.hpp"
#include "VirtioPciTransport.hpp"
#include "vv/TestPattern.hpp"

namespace {
namespace v = vv::virtio;
using namespace vv::gpudevice;

constexpr uint32_t kPatternShowMs = 15000;
constexpr uint32_t kPatternDisabledMs = 10000;
constexpr uint32_t kPatternSleepStepMs = 100;
constexpr uint32_t kPatternScanout = 0;
constexpr size_t kPatternHistoryBytes = 256;
} // namespace

void VirtioGpuDevice::patternStage(const char *stage)
{
    // 단계 기록을 이어 붙여 하나의 속성으로 남긴다 (예: "shown scanout-disabled released reinitialized").
    OSString *previous = OSDynamicCast(OSString, getProperty("VVPatternTest"));
    char history[kPatternHistoryBytes];
    snprintf(history, sizeof(history), "%s%s%s", previous != nullptr ? previous->getCStringNoCopy() : "",
             previous != nullptr ? " " : "", stage);
    setProperty("VVPatternTest", history);
    VV_LOG_INFO("gpu: pattern test stage %s", stage);
    publishCounters();
    vv::publishLog(this);
}

void VirtioGpuDevice::patternTimerFired(OSObject *owner, IOTimerEventSource *)
{
    // 워크루프 문맥이라 명령을 기다릴 수 없다. 시험은 thread_call에서 돈다.
    auto *self = OSDynamicCast(VirtioGpuDevice, owner);
    if (self != nullptr && self->mPatternCall != nullptr && !self->mStopping) {
        thread_call_enter(self->mPatternCall);
    }
}

void VirtioGpuDevice::deferredPatternTest(thread_call_param_t owner, thread_call_param_t)
{
    auto *self = static_cast<VirtioGpuDevice *>(owner);
    OSIncrementAtomic(&self->mDisplayQueryActive);
    if (!self->mStopping) {
        self->runPatternTest();
    }
    OSDecrementAtomic(&self->mDisplayQueryActive);
}

IOReturn VirtioGpuDevice::gatedReinitialize(OSObject *owner, void *, void *, void *, void *)
{
    // 복구와 같은 순서이지만 복구 횟수에 넣지 않는다 (시험 절차의 일부).
    auto *self = OSDynamicCast(VirtioGpuDevice, owner);
    if (self == nullptr || self->mTransport == nullptr || self->mGivenUp) {
        return kIOReturnNotReady;
    }
    self->mTransport->reset();
    self->releaseQueues();
    const char *failure = nullptr;
    if (!self->bringUpDevice(&failure)) {
        self->giveUpDevice(failure);
        return kIOReturnError;
    }
    return kIOReturnSuccess;
}

void VirtioGpuDevice::runPatternTest()
{
    if (mFramebuffer == nullptr || !vv::display::isUsable(mFramebufferMode)) {
        patternStage("no-framebuffer");
        return;
    }
    const vv::display::Mode mode = mFramebufferMode;
    vv::display::fillPattern(static_cast<uint32_t *>(mFramebuffer->cpuAddress()), mode.width, mode.height,
                             vv::display::rowBytes(mode) / vv::display::kBytesPerPixel);

    const uint32_t resourceId = allocateResourceId();
    auto sleepUnlessStopping = [this](uint32_t milliseconds) {
        for (uint32_t waited = 0; waited < milliseconds && !mStopping; waited += kPatternSleepStepMs) {
            IOSleep(kPatternSleepStepMs);
        }
    };

    // 1) 화면에 내보낸다. 한 단계라도 실패하면 거기서 멈추고 만든 것을 정리한다.
    bool created = false;
    bool attached = false;
    const char *failedStep = nullptr;
    if (createResource2d(resourceId, v::gpu::Format::B8G8R8X8Unorm, mode) != kIOReturnSuccess) {
        failedStep = "failed:create";
    } else {
        created = true;
        if (attachBacking(resourceId, mFramebuffer->deviceAddress(), static_cast<uint32_t>(mFramebuffer->length())) !=
            kIOReturnSuccess) {
            failedStep = "failed:attach";
        } else {
            attached = true;
            if (transferToHost(resourceId, mode) != kIOReturnSuccess) {
                failedStep = "failed:transfer";
            } else if (setScanout(kPatternScanout, resourceId, mode) != kIOReturnSuccess) {
                failedStep = "failed:set-scanout";
            } else if (flushResource(resourceId, mode) != kIOReturnSuccess) {
                failedStep = "failed:flush";
            }
        }
    }
    if (failedStep != nullptr) {
        patternStage(failedStep);
    } else {
        patternStage("shown");
        sleepUnlessStopping(kPatternShowMs);
    }

    // 2) scanout을 끈다 (실패 경로에서도 한 번 시도한다).
    patternStage(setScanout(kPatternScanout, 0, mode) == kIOReturnSuccess ? "scanout-disabled"
                                                                          : "failed:scanout-disable");
    sleepUnlessStopping(kPatternDisabledMs);

    // 3) resource를 정리한다.
    if (attached) {
        detachBacking(resourceId);
    }
    if (created) {
        unrefResource(resourceId);
    }
    patternStage("released");

    // 4) 장치를 reset하고 다시 올린다.
    if (!mStopping && mGate != nullptr) {
        patternStage(mGate->runAction(&VirtioGpuDevice::gatedReinitialize) == kIOReturnSuccess
                         ? "reinitialized"
                         : "failed:reinitialize");
    }
}
