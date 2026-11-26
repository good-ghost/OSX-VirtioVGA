// 부트 프레임버퍼 넘겨받기 (WI-2.006 범위를 WI-2.005 시험에 함께 넣음, R-06).
// boot-arg `vvideo_takeover=1`일 때만 동작한다.
//
// **주의 (2026-11-24 실측)**: 실행 중 IONDRV를 terminate하면 IOGraphics 종료 알림을 처리하던
// AppleUpstreamUserClient가 NULL을 참조해 패닉할 수 있다 (PHASE-PLAN-02). 기본 경로는 OpenCore
// DeviceProperties로 부팅 때부터 AAPL,iokit-ignore-ndrv를 넣는 것이고(VirtioGpuDevice::requestFramebufferMatch),
// 이 파일의 넘겨받기는 시험용으로만 남긴다.
//
// 우리 kext는 보조 kernel collection으로 부팅 후반에 로드된다. 그때 IONDRVFramebuffer가 이미 같은 PCI 장치에
// IOFramebuffer 카테고리로 붙어 있어 점수만으로는 대체되지 않는다. 그래서:
//   1) 드라이버 시작 kTakeoverDelayMs 뒤 시작한다. 이 시험이 패닉을 내면 매 부팅 되풀이되므로 그 사이
//      SSH나 Recovery로 boot-args를 고칠 시간을 주려는 것이다.
//   2) PCI 장치에 AAPL,iokit-ignore-ndrv를 붙여 IONDRV가 다시 붙지 못하게 한다 (IOGraphics 소스 근거, WI-2.004.md §4.1).
//   3) IONDRVFramebuffer를 종료하고 장치에서 떨어질 때까지 기다린다.
//   4) PCI 장치 매칭을 다시 돌려 우리 프레임버퍼(점수 30000)가 붙게 한다.
//   5) 일정 시간 안에 붙지 않으면 속성을 지우고 매칭을 다시 돌려 IONDRV가 돌아오게 한다.
// 각 단계는 VVTakeover 속성과 로그에 남는다.
#include "VirtioGpuDevice.hpp"
#include "VirtioGpuDevicePrivate.hpp"

#include <IOKit/IOTimerEventSource.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/OSAtomic.h>

#include "VVBootArgs.hpp"
#include "VVLog.hpp"

namespace {
using namespace vv::gpudevice;

using vv::kIgnoreNdrvProperty;
constexpr const char *kNdrvClassName = "IONDRVFramebuffer";
constexpr uint32_t kDetachWaitMs = 10000;
constexpr uint32_t kAttachWaitMs = 10000;
constexpr uint32_t kWaitStepMs = 100;
constexpr size_t kTakeoverHistoryBytes = 256;

// PCI 장치에 붙은 IONDRVFramebuffer를 retain해서 돌려준다 (없으면 nullptr).
IOService *copyNdrvClient(IOPCIDevice *pci)
{
    OSIterator *clients = pci->getClientIterator();
    if (clients == nullptr) {
        return nullptr;
    }
    IOService *found = nullptr;
    while (OSObject *object = clients->getNextObject()) {
        auto *client = OSDynamicCast(IOService, object);
        if (client != nullptr && client->metaCast(kNdrvClassName) != nullptr) {
            client->retain();
            found = client;
            break;
        }
    }
    clients->release();
    return found;
}
} // namespace

void VirtioGpuDevice::takeoverStage(const char *stage)
{
    OSString *previous = OSDynamicCast(OSString, getProperty("VVTakeover"));
    char history[kTakeoverHistoryBytes];
    snprintf(history, sizeof(history), "%s%s%s", previous != nullptr ? previous->getCStringNoCopy() : "",
             previous != nullptr ? " " : "", stage);
    setProperty("VVTakeover", history);
    VV_LOG_INFO("gpu: takeover stage %s", stage);
    vv::publishLog(this);
}

void VirtioGpuDevice::takeoverTimerFired(OSObject *owner, IOTimerEventSource *)
{
    // 워크루프 문맥에서는 다른 서비스의 종료를 기다릴 수 없으므로 thread_call에서 진행한다.
    auto *self = OSDynamicCast(VirtioGpuDevice, owner);
    if (self != nullptr && self->mTakeoverCall != nullptr && !self->mStopping) {
        thread_call_enter(self->mTakeoverCall);
    }
}

void VirtioGpuDevice::deferredTakeover(thread_call_param_t owner, thread_call_param_t)
{
    auto *self = static_cast<VirtioGpuDevice *>(owner);
    OSIncrementAtomic(&self->mDisplayQueryActive);
    if (!self->mStopping) {
        self->runTakeover();
    }
    OSDecrementAtomic(&self->mDisplayQueryActive);
}

void VirtioGpuDevice::runTakeover()
{
    if (mPci == nullptr || !isReadyForFramebuffer()) {
        takeoverStage("skipped:device-not-ready");
        return;
    }
    if (mFramebufferClient != nullptr) {
        takeoverStage("skipped:already-attached");
        return;
    }

    // 2) IONDRV가 다시 붙지 못하게 한다.
    mPci->setProperty(kIgnoreNdrvProperty, kOSBooleanTrue);
    takeoverStage("ignore-ndrv-set");

    // 3) IONDRV를 종료하고 떨어질 때까지 기다린다.
    IOService *ndrv = copyNdrvClient(mPci);
    if (ndrv == nullptr) {
        takeoverStage("no-ndrv");
    } else {
        const bool terminated = ndrv->terminate();
        takeoverStage(terminated ? "ndrv-terminate-requested" : "ndrv-terminate-refused");
        uint32_t waited = 0;
        while (waited < kDetachWaitMs && !mStopping && ndrv->getProvider() != nullptr) {
            IOSleep(kWaitStepMs);
            waited += kWaitStepMs;
        }
        takeoverStage(ndrv->getProvider() == nullptr ? "ndrv-detached" : "ndrv-still-attached");
        ndrv->release();
    }

    // 4) 매칭을 다시 돌린다.
    mPci->registerService();
    takeoverStage("rematch");
    uint32_t waited = 0;
    while (waited < kAttachWaitMs && !mStopping && mFramebufferClient == nullptr) {
        IOSleep(kWaitStepMs);
        waited += kWaitStepMs;
    }
    if (mFramebufferClient != nullptr) {
        takeoverStage("framebuffer-attached");
        return;
    }

    // 5) 붙지 않았다. IONDRV가 돌아올 수 있게 되돌린다.
    takeoverStage("framebuffer-not-attached");
    mPci->removeProperty(kIgnoreNdrvProperty);
    mPci->registerService();
    takeoverStage("ndrv-restore-requested");
}

void VirtioGpuDevice::requestFramebufferMatch()
{
    if (mPci == nullptr || mFramebufferMatchCall == nullptr || mPci->getProperty(kIgnoreNdrvProperty) == nullptr) {
        return;
    }
    setProperty("VVFramebufferMatch", "scheduled");
    thread_call_enter(mFramebufferMatchCall);
}

void VirtioGpuDevice::deferredFramebufferMatch(thread_call_param_t owner, thread_call_param_t)
{
    // start() 안(매칭 진행 중)에서 같은 PCI 장치의 registerService를 부르지 않도록 thread_call로 미룬다.
    auto *self = static_cast<VirtioGpuDevice *>(owner);
    OSIncrementAtomic(&self->mDisplayQueryActive);
    if (!self->mStopping && self->mFramebufferClient == nullptr && self->isReadyForFramebuffer()) {
        self->mPci->registerService();
        self->setProperty("VVFramebufferMatch", "rematch-requested");
        VV_LOG_INFO("gpu: framebuffer rematch requested (ignore-ndrv present)");
    } else {
        self->setProperty("VVFramebufferMatch", self->mFramebufferClient != nullptr ? "already-attached" : "not-ready");
    }
    vv::publishLog(self);
    OSDecrementAtomic(&self->mDisplayQueryActive);
}

void VirtioGpuDevice::restoreNdrv(const char *reason)
{
    // 보조 kext(VirtioVGANdrvBlocker)가 붙인 속성을 지우고 매칭을 다시 돌린다. IONDRV는 새로 붙기만 하므로
    // 실행 중 종료(패닉 위험, PHASE-PLAN-02)와 달리 안전하다. start() 안에서도 부를 수 있도록 비동기로 등록한다.
    if (mPci == nullptr || mPci->getProperty(kIgnoreNdrvProperty) == nullptr) {
        return;
    }
    // 보조 kext가 재매칭 때 다시 막지 않도록 표시를 먼저 남긴다.
    mPci->setProperty(vv::kNdrvHandedBackProperty, kOSBooleanTrue);
    mPci->removeProperty(kIgnoreNdrvProperty);
    char state[kSummaryBytes];
    snprintf(state, sizeof(state), "restored:%s", reason);
    mPci->setProperty("VVNdrvBlocked", state);
    setProperty("VVNdrvRestore", reason);
    mPci->registerService(kIOServiceAsynchronous);
    VV_LOG_ERR("gpu: our display path unavailable (%s), handing the screen back to IONDRV", reason);
}
