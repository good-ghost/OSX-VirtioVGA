#include "VirtioGpuDevice.hpp"
#include "VirtioGpuDevicePrivate.hpp"

#include <IOKit/IOCommandGate.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/OSAtomic.h>
#include <pexpert/pexpert.h>

#include "VVBootArgs.hpp"
#include "VVDmaBuffer.hpp"
#include "VVLog.hpp"
#include "VirtioPciTransport.hpp"
#include "VirtioQueue.hpp"
#include "vv/GpuCommand.hpp"
#include "vv/PciCapWalk.hpp"
#include "vv/VirtioFeatures.hpp"
#include "vv/VirtioGpuSpec.hpp"

OSDefineMetaClassAndStructors(VirtioGpuDevice, IOService)

namespace {

namespace v = vv::virtio;
using namespace vv::gpudevice;

// 상태 감시 주기. 외부(Apple 전송 드라이버 등)가 장치를 reset하는지 보기 위한 것이라 촘촘할 필요가 없다.
constexpr uint32_t kStatusWatchIntervalMs = 2000;
// Poll 방식의 확인 주기 (디버그용이라 성능보다 단순함을 택했다).
constexpr uint32_t kPollIntervalMs = 10;
// 원하는 큐 크기. 장치 최대가 더 작으면 그에 맞춘다. 명령은 한 번에 하나씩 보내므로(REQ-024) 크게 둘 필요가 없다.
constexpr uint16_t kDesiredQueueSize[] = {64, 16};  // controlq, cursorq
constexpr const char *kQueueProperty[] = {"VVControlQueue", "VVCursorQueue"};
// 인터럽트 소스 목록을 훑을 최대 인덱스 (레거시 1 + MSI-X 테이블 크기보다 넉넉히).
constexpr int kMaxInterruptSourceIndex = 16;

// 복구 경로 시험용 boot-arg와 지연 (WI-1.006). 부팅이 끝나고 상태 감시가 돌기 시작한 뒤에 흉내 낸다.
constexpr const char *kTestResetBootArg = "vvideo_test_reset";
constexpr uint32_t kTestResetDelayMs = 30000;
// 시험 패턴 boot-arg와 시작 지연. 로그인하고 QEMU 창을 볼 시간을 준다.
constexpr const char *kTestPatternBootArg = "vvideo_test_pattern";
constexpr uint32_t kPatternStartDelayMs = 90000;
// 부트 프레임버퍼 넘겨받기 boot-arg와 시작 지연 (VirtioGpuDeviceTakeover.cpp 머리말).
constexpr const char *kTakeoverBootArg = "vvideo_takeover";
constexpr uint32_t kTakeoverDelayMs = 60000;
// stop에서 실행 중인 확인 명령이 끝나기를 기다리는 한도. 명령 기한(kCommandTimeoutMs)보다 넉넉히 잡는다.
constexpr uint32_t kDisplayQueryDrainMs = 3000;
constexpr uint32_t kDrainPollMs = 1;


static_assert(v::gpu::kControlQueue == 0 && v::gpu::kCursorQueue == 1, "mQueues 배열 순서");

uint32_t high32(uint64_t value)
{
    return static_cast<uint32_t>(value >> 32);
}

uint32_t low32(uint64_t value)
{
    return static_cast<uint32_t>(value);
}

bool bootArgEnabled(const char *name)
{
    int value = 0;
    return PE_parse_boot_argn(name, &value, sizeof(value)) && value != 0;
}

bool testResetRequested()
{
    return bootArgEnabled(kTestResetBootArg);
}

v::IrqMode irqModeFromBootArgs()
{
    int value = 0;
    const bool hasValue = PE_parse_boot_argn(v::kIrqModeBootArg, &value, sizeof(value));
    return v::irqModeFromBootArg(hasValue, value);
}

} // namespace

IOService *VirtioGpuDevice::probe(IOService *provider, SInt32 *score)
{
    if (vv::isDriverDisabledByBootArg()) {
        return nullptr;
    }
    return IOService::probe(provider, score);
}

IOWorkLoop *VirtioGpuDevice::getWorkLoop() const
{
    return mWorkLoop;
}

bool VirtioGpuDevice::start(IOService *provider)
{
    if (!IOService::start(provider)) {
        return false;
    }
    if (!vv::initLog()) {
        setProperty("VVLogInitFailed", true);
    }
    mPci = OSDynamicCast(IOPCIDevice, provider);
    if (mPci == nullptr) {
        failDevice("provider-not-pci");
        return true;
    }
    mPci->retain();
    mWorkLoop = IOWorkLoop::workLoop();
    if (mWorkLoop == nullptr) {
        failDevice("workloop");
        return true;
    }
    publishPciDiagnostics(mPci);

    v::pci::LayoutError layoutError = v::pci::LayoutError::None;
    mTransport = VirtioPciTransport::withDevice(mPci, sizeof(v::gpu::Config), &layoutError);
    setProperty("VVLayoutResult", v::pci::layoutErrorName(layoutError));
    if (mTransport == nullptr) {
        failDevice("transport");
        return true;
    }
    mIrqMode = irqModeFromBootArgs();
    mGate = IOCommandGate::commandGate(this);
    mCommandBuffer = VVDmaBuffer::withLength(kCommandBufferBytes);
    if (mGate == nullptr || mWorkLoop->addEventSource(mGate) != kIOReturnSuccess || mCommandBuffer == nullptr) {
        failDevice("command-setup");
        return true;
    }
    mDisplayQueryCall = thread_call_allocate(&VirtioGpuDevice::deferredDisplayQuery, this);
    mPatternCall = thread_call_allocate(&VirtioGpuDevice::deferredPatternTest, this);
    mFlushCall = thread_call_allocate(&VirtioGpuDevice::deferredFlushLoop, this);
    mTakeoverCall = thread_call_allocate(&VirtioGpuDevice::deferredTakeover, this);
    if (mDisplayQueryCall == nullptr || mPatternCall == nullptr || mFlushCall == nullptr ||
        mTakeoverCall == nullptr) {
        failDevice("thread-call");
        return true;
    }
    if (!prepareInterrupts()) {
        failDevice("interrupts");
        return true;
    }
    const char *failure = nullptr;
    if (!bringUpDevice(&failure)) {
        failDevice(failure);
        return true;
    }
    enableInterrupts();
    startTimers();
    queryDisplayInfo();
    runSelfTest();
    probeContiguousAllocation();
    allocateFramebuffer(mPreferredMode);
    publishCounters();
    vv::publishLog(this);
    return true;
}

bool VirtioGpuDevice::bringUpDevice(const char **failureReason)
{
    // §3.1.1 순서대로 장치를 올린다. 처음 시작과 복구(WI-1.006)에서 같이 쓴다. 인터럽트 소스는 미리 만들어 둔다.
    auto fail = [failureReason](const char *reason) {
        *failureReason = reason;
        return false;
    };
    VV_LOG_INFO("gpu: status before reset=0x%02x", mTransport->status());
    // 1단계: reset. 펌웨어나 Apple 전송 드라이버가 남긴 상태(관찰값 ACK)를 가정하지 않는다.
    if (!mTransport->reset()) {
        return fail("reset");
    }
    mTransport->addStatus(v::status::kAcknowledge);
    mTransport->addStatus(v::status::kDriver);

    const uint64_t deviceFeatures = mTransport->readDeviceFeatures();
    setProperty("VVDeviceFeatures", deviceFeatures, 64);
    VV_LOG_INFO("gpu: device features hi=0x%08x lo=0x%08x", high32(deviceFeatures), low32(deviceFeatures));

    const auto negotiation = v::negotiateFeatures(deviceFeatures);
    setProperty("VVNegotiation", v::negotiationErrorName(negotiation.error));
    if (negotiation.error != v::NegotiationError::None) {
        return fail(v::negotiationErrorName(negotiation.error));
    }
    if (!mTransport->writeDriverFeatures(negotiation.driverFeatures)) {
        return fail("write-features");
    }
    const uint64_t readBack = mTransport->readDriverFeatures();
    setProperty("VVDriverFeatures", readBack, 64);
    VV_LOG_INFO("gpu: driver features hi=0x%08x lo=0x%08x (wanted lo=0x%08x)", high32(readBack), low32(readBack),
                low32(negotiation.driverFeatures));

    mTransport->addStatus(v::status::kFeaturesOk);
    // 6단계: FEATURES_OK를 다시 읽어 장치가 feature 조합을 받아들였는지 확인한다.
    const uint8_t status = mTransport->status();
    if ((status & v::status::kFeaturesOk) == 0) {
        return fail("features-not-accepted");
    }
    VV_LOG_INFO("gpu: FEATURES_OK accepted, status=0x%02x", status);

    // 벡터는 reset 때마다 NO_VECTOR로 돌아가므로 매번 다시 쓴다. MSI-X가 아니면 NO_VECTOR로 둔다.
    if (!mTransport->setConfigVector(v::configVectorFor(mIrqMode))) {
        VV_LOG_ERR("gpu: config vector 0x%x not accepted", v::configVectorFor(mIrqMode));
        return fail("config-vector");
    }
    if (!setupQueues()) {
        return fail("queues");
    }
    // 8단계: 장치별 설정(큐)을 마친 뒤 DRIVER_OK. 이 뒤로 장치는 큐를 쓸 수 있다.
    mTransport->addStatus(v::status::kDriverOk);
    const uint8_t live = mTransport->status();
    if (v::classifyStatus(live) != v::DeviceHealth::Ok) {
        return fail("driver-ok");
    }
    mExpectedStatus = live;
    setProperty("VVInitResult", "driver-ok");
    setProperty("VVDeviceStatus", live, 8);
    VV_LOG_INFO("gpu: DRIVER_OK, status=0x%02x irq=%s", live, v::irqModeName(mIrqMode));
    return true;
}

void VirtioGpuDevice::releaseQueues()
{
    for (VirtioQueue *&queue : mQueues) {
        if (queue != nullptr) {
            queue->release();
            queue = nullptr;
        }
    }
}

void VirtioGpuDevice::recoverDevice(const char *reason)
{
    // 워크루프 문맥(타이머, 인터럽트 처리기, 게이트 안)에서만 부른다.
    if (mGivenUp || mTransport == nullptr) {
        return;
    }
    if (!v::mayRecover(mRecoveries)) {
        giveUpDevice("recovery-limit");
        return;
    }
    ++mRecoveries;
    setProperty("VVRecoveries", mRecoveries, 32);
    setProperty("VVLastRecoveryReason", reason);
    VV_LOG_ERR("gpu: recovering device (%s), attempt %u/%u", reason, mRecoveries, v::kMaxRecoveries);

    // 장치가 큐 메모리를 더 쓰지 않도록 먼저 reset하고, 걸려 있던 명령은 버린다.
    mTransport->reset();
    if (mCommandInFlight) {
        mCommandInFlight = false;
        mCommandDone = true;
        mGate->commandWakeup(&mCommandDone);
    }
    releaseQueues();

    const char *failure = nullptr;
    if (!bringUpDevice(&failure)) {
        VV_LOG_ERR("gpu: recovery bring-up failed: %s", failure);
        giveUpDevice(failure);
        return;
    }
    VV_LOG_INFO("gpu: recovery %u done", mRecoveries);
    // 게이트 안에서는 명령 완료를 기다릴 수 없으므로(게이트 재진입), 확인용 명령은 워크루프 밖에서 보낸다.
    thread_call_enter(mDisplayQueryCall);
    vv::publishLog(this);
}

void VirtioGpuDevice::giveUpDevice(const char *reason)
{
    // 타이머 처리기 안에서도 부를 수 있으므로 이벤트 소스는 건드리지 않는다. 장치를 FAILED로 두고 큐를 놓는다.
    mGivenUp = true;
    VV_LOG_ERR("gpu: giving up on device: %s", reason);
    setProperty("VVInitResult", reason);
    if (mTransport != nullptr) {
        mTransport->addStatus(v::status::kFailed);
        mTransport->reset();
    }
    if (mCommandInFlight) {
        mCommandInFlight = false;
        mCommandDone = true;
        mGate->commandWakeup(&mCommandDone);
    }
    releaseQueues();
    vv::publishLog(this);
}

void VirtioGpuDevice::deferredDisplayQuery(thread_call_param_t owner, thread_call_param_t)
{
    auto *self = static_cast<VirtioGpuDevice *>(owner);
    OSIncrementAtomic(&self->mDisplayQueryActive);
    if (!self->mStopping) {
        self->queryDisplayInfo();
    }
    OSDecrementAtomic(&self->mDisplayQueryActive);
}

void VirtioGpuDevice::findInterruptSources()
{
    mMsixSourceCount = 0;
    mLegacyIndex = kNoSource;
    for (int index = 0; index < kMaxInterruptSourceIndex; ++index) {
        int type = 0;
        if (mPci->getInterruptType(index, &type) != kIOReturnSuccess) {
            break;
        }
        if ((type & kIOInterruptTypePCIMessagedX) != 0) {
            if (mMsixSourceCount < v::kSeparateMsixVectors) {
                mMsixIndices[mMsixSourceCount] = index;
            }
            ++mMsixSourceCount;
        } else if ((type & kIOInterruptTypePCIMessaged) == 0 && mLegacyIndex == kNoSource) {
            mLegacyIndex = index;  // 레벨(또는 에지) 레거시 소스
        }
    }
}

bool VirtioGpuDevice::prepareInterrupts()
{
    findInterruptSources();
    publishInterruptSources();
    const v::IrqMode requested = mIrqMode;
    mIrqMode = v::effectiveIrqMode(requested, mMsixSourceCount, mLegacyIndex != kNoSource);
    VV_LOG_INFO("gpu: irq requested=%s effective=%s (msix sources=%u, legacy index=%d)", v::irqModeName(requested),
                v::irqModeName(mIrqMode), mMsixSourceCount, mLegacyIndex);
    if (mIrqMode == v::IrqMode::MsiX && !prepareMsix()) {
        return false;
    }
    if (mIrqMode == v::IrqMode::Intx && !prepareIntx()) {
        return false;
    }
    // 장치에 벡터를 쓰는 것은 bringUpDevice에서 한다 (reset 때마다 다시 써야 하므로).
    setProperty("VVIrqMode", v::irqModeName(mIrqMode));
    setProperty("VVIrqSharedVector", mIrqMode == v::IrqMode::MsiX && !v::usesSeparateVectors(mMsixSourceCount));
    return true;
}

bool VirtioGpuDevice::prepareMsix()
{
    const uint16_t sources = v::usesSeparateVectors(mMsixSourceCount) ? v::kSeparateMsixVectors : 1;
    for (uint16_t vector = 0; vector < sources; ++vector) {
        IOInterruptEventSource *source = IOInterruptEventSource::interruptEventSource(
            this, &VirtioGpuDevice::msixFired, mPci, mMsixIndices[vector]);
        if (source == nullptr || mWorkLoop->addEventSource(source) != kIOReturnSuccess) {
            VV_LOG_ERR("gpu: MSI-X source for vector %u (index %d) failed", vector, mMsixIndices[vector]);
            if (source != nullptr) {
                source->release();
            }
            return false;
        }
        mMsixSources[vector] = source;
    }
    VV_LOG_INFO("gpu: MSI-X %s, %u event source(s)", sources == 1 ? "shared vector 0" : "separate vectors",
                sources);
    return true;
}

bool VirtioGpuDevice::prepareIntx()
{
    // 레거시 라인은 다른 장치와 공유될 수 있으므로 필터에서 ISR로 우리 인터럽트인지 가린다.
    mIntxSource = IOFilterInterruptEventSource::filterInterruptEventSource(
        this, &VirtioGpuDevice::intxFired, &VirtioGpuDevice::intxFilter, mPci, mLegacyIndex);
    if (mIntxSource == nullptr || mWorkLoop->addEventSource(mIntxSource) != kIOReturnSuccess) {
        VV_LOG_ERR("gpu: INTx source (index %d) failed", mLegacyIndex);
        return false;
    }
    return true;
}

bool VirtioGpuDevice::setupQueues()
{
    const uint16_t queueCount = mTransport->numQueues();
    setProperty("VVNumQueues", queueCount, 16);
    if (queueCount < kQueueCount) {
        VV_LOG_ERR("gpu: device has %u queues, need %u", queueCount, kQueueCount);
        return false;
    }
    for (uint16_t index = 0; index < kQueueCount; ++index) {
        const uint16_t maxSize = mTransport->queueMaxSize(index);
        const uint16_t size = v::pci::chooseQueueSize(kDesiredQueueSize[index], maxSize);
        if (size == 0) {
            VV_LOG_ERR("gpu: queue %u not available (max %u)", index, maxSize);
            return false;
        }
        mQueues[index] = VirtioQueue::withSize(index, size);
        if (mQueues[index] == nullptr) {
            VV_LOG_ERR("gpu: queue %u allocation failed", index);
            return false;
        }
        const uint16_t vector = v::queueVectorFor(mIrqMode, index, mMsixSourceCount);
        if (!mTransport->setupQueue(mQueues[index], vector)) {
            return false;
        }
        char summary[kSummaryBytes];
        snprintf(summary, sizeof(summary), "size=%u max=%u notify=0x%x vector=0x%x enabled=%d", size, maxSize,
                 mQueues[index]->notifyOffset(), vector, mTransport->isQueueEnabled(index));
        setProperty(kQueueProperty[index], summary);
    }
    return true;
}

void VirtioGpuDevice::enableInterrupts()
{
    for (IOInterruptEventSource *source : mMsixSources) {
        if (source != nullptr) {
            source->enable();
        }
    }
    if (mIntxSource != nullptr) {
        mIntxSource->enable();
    }
}

void VirtioGpuDevice::releaseInterrupts()
{
    for (IOInterruptEventSource *&source : mMsixSources) {
        if (source != nullptr) {
            source->disable();
            mWorkLoop->removeEventSource(source);
            source->release();
            source = nullptr;
        }
    }
    if (mIntxSource != nullptr) {
        mIntxSource->disable();
        mWorkLoop->removeEventSource(mIntxSource);
        mIntxSource->release();
        mIntxSource = nullptr;
    }
}

void VirtioGpuDevice::publishInterruptSources()
{
    char summary[kSummaryBytes] = {};
    size_t used = 0;
    for (int index = 0; index < kMaxInterruptSourceIndex && used + 16 < sizeof(summary); ++index) {
        int type = 0;
        if (mPci->getInterruptType(index, &type) != kIOReturnSuccess) {
            break;
        }
        const char *kind = (type & kIOInterruptTypePCIMessagedX) != 0 ? "msix"
                           : (type & kIOInterruptTypePCIMessaged) != 0 ? "msi"
                           : (type & kIOInterruptTypeLevel) != 0 ? "level"
                                                                 : "edge";
        const int written =
            snprintf(summary + used, sizeof(summary) - used, "%s%d:%s", used == 0 ? "" : " ", index, kind);
        if (written <= 0) {
            break;
        }
        used += static_cast<size_t>(written);
    }
    setProperty("VVInterruptSources", summary);
    VV_LOG_INFO("gpu: interrupt sources %s", summary);
}

void VirtioGpuDevice::handleQueueEvent(unsigned queue)
{
    if (queue >= kQueueCount || mQueues[queue] == nullptr) {
        return;
    }
    ++mQueueEvents[queue];
    v::RingCompletion completion{};
    for (;;) {
        const auto result = mQueues[queue]->ring().collect(completion);
        if (result == v::SplitRing::CollectResult::Empty) {
            break;
        }
        if (result == v::SplitRing::CollectResult::DeviceError) {
            ++mDeviceErrors;
            VV_LOG_ERR("gpu: queue %u returned out-of-range used id", queue);
            // 링 상태를 더 믿을 수 없다. 복구가 큐를 새로 만들므로 이 반복은 여기서 끝낸다.
            recoverDevice("used-id-out-of-range");
            return;
        }
        ++mCompletions[queue];
        if (queue == v::gpu::kControlQueue && mCommandInFlight && completion.head == mCommandHead) {
            mCommandWritten = completion.written;
            mCommandDone = true;
            mGate->commandWakeup(&mCommandDone);
        }
    }
}

void VirtioGpuDevice::handleConfigEvent()
{
    using Gpu = v::gpu::Config;
    ++mConfigEvents;
    // §2.1.2: 장치는 DEVICE_NEEDS_RESET을 올릴 때 config 변경 알림을 보낸다.
    const auto health = v::classifyStatus(mTransport->status());
    if (health != v::DeviceHealth::Ok) {
        recoverDevice(v::deviceHealthName(health));
        return;
    }
    const uint32_t events = mTransport->deviceRead32(offsetof(Gpu, eventsRead));
    if (events != 0) {
        VV_LOG_INFO("gpu: config event 0x%x (display=%d)", events, (events & v::gpu::kEventDisplay) != 0);
        // §5.7.4: 처리한 비트를 events_clear에 써서 지운다. 디스플레이 정보 재조회는 WI-3.005.
        mTransport->deviceWrite32(offsetof(Gpu, eventsClear), events);
        vv::publishLog(this);
    }
}

void VirtioGpuDevice::handleSharedEvent()
{
    // 공유 벡터는 원인을 알려 주지 않으므로 config 이벤트와 두 큐를 모두 확인한다.
    using Gpu = v::gpu::Config;
    if (mTransport->deviceRead32(offsetof(Gpu, eventsRead)) != 0) {
        handleConfigEvent();
    }
    for (unsigned queue = 0; queue < kQueueCount; ++queue) {
        if (mQueues[queue] != nullptr && mQueues[queue]->ring().pendingCompletions() != 0) {
            handleQueueEvent(queue);
        }
    }
}

void VirtioGpuDevice::msixFired(OSObject *owner, IOInterruptEventSource *sender, int)
{
    auto *self = OSDynamicCast(VirtioGpuDevice, owner);
    if (self == nullptr || self->mTransport == nullptr) {
        return;
    }
    ++self->mInterrupts;
    if (!v::usesSeparateVectors(self->mMsixSourceCount)) {
        self->handleSharedEvent();
    } else if (sender == self->mMsixSources[v::kConfigVector]) {
        self->handleConfigEvent();
    } else if (sender == self->mMsixSources[v::kControlQueueVector]) {
        self->handleQueueEvent(v::gpu::kControlQueue);
    } else if (sender == self->mMsixSources[v::kCursorQueueVector]) {
        self->handleQueueEvent(v::gpu::kCursorQueue);
    }
}

bool VirtioGpuDevice::intxFilter(OSObject *owner, IOFilterInterruptEventSource *)
{
    // 1차 인터럽트 문맥. 로그·할당·락을 쓰지 않는다. ISR을 읽어(읽으면 지워짐) 우리 것이면 원인을 쌓는다.
    auto *self = static_cast<VirtioGpuDevice *>(owner);
    if (self->mTransport == nullptr) {
        return false;
    }
    const uint8_t isr = self->mTransport->readIsr();
    if (isr == 0) {
        return false;  // 공유 라인의 다른 장치 인터럽트
    }
    OSBitOrAtomic(isr, &self->mIntxPendingIsr);
    return true;
}

void VirtioGpuDevice::intxFired(OSObject *owner, IOInterruptEventSource *, int)
{
    auto *self = OSDynamicCast(VirtioGpuDevice, owner);
    if (self == nullptr) {
        return;
    }
    ++self->mInterrupts;
    const auto isr = static_cast<uint8_t>(OSBitAndAtomic(0, &self->mIntxPendingIsr));
    if (v::isrHasConfigEvent(isr)) {
        self->handleConfigEvent();
    }
    if (v::isrHasQueueEvent(isr)) {
        // ISR은 어느 큐인지 알려 주지 않으므로 둘 다 확인한다.
        self->handleQueueEvent(v::gpu::kControlQueue);
        self->handleQueueEvent(v::gpu::kCursorQueue);
    }
}

void VirtioGpuDevice::pollTimerFired(OSObject *owner, IOTimerEventSource *sender)
{
    auto *self = OSDynamicCast(VirtioGpuDevice, owner);
    if (self == nullptr || self->mTransport == nullptr) {
        return;
    }
    self->handleSharedEvent();
    sender->setTimeoutMS(kPollIntervalMs);
}

bool VirtioGpuDevice::allocateFramebuffer(const vv::display::Mode &mode)
{
    // WI-2.003: 현재 모드 크기의 물리 연속 버퍼. resource에 붙이는 것(ATTACH_BACKING)은 WI-2.004.
    if (!vv::display::isUsable(mode) || !vv::display::fitsInPool(mode)) {
        VV_LOG_ERR("gpu: framebuffer mode %ux%u not allowed", mode.width, mode.height);
        return false;
    }
    const uint64_t bytes = vv::display::framebufferBytes(mode);
    VVDmaBuffer *buffer = VVDmaBuffer::withLength(static_cast<size_t>(bytes));
    char summary[kSummaryBytes];
    if (buffer == nullptr) {
        snprintf(summary, sizeof(summary), "%ux%u alloc-failed", mode.width, mode.height);
        setProperty("VVFramebuffer", summary);
        VV_LOG_ERR("gpu: framebuffer %ux%u (%llu bytes) allocation failed", mode.width, mode.height, bytes);
        return false;
    }
    if (mFramebuffer != nullptr) {
        mFramebuffer->release();
    }
    mFramebuffer = buffer;
    mFramebufferMode = mode;
    // 장치 주소와 CPU 물리 주소가 같으면 IOMMU 변환이 없다는 뜻이다 (aperture에 물리 주소를 써도 된다).
    const bool identity = buffer->physicalAddress() != 0 && buffer->physicalAddress() == buffer->deviceAddress();
    snprintf(summary, sizeof(summary), "%ux%u bytes=%zu contiguous=%d dma-identity=%d", mode.width, mode.height,
             buffer->length(), buffer->physicalAddress() != 0, identity);
    setProperty("VVFramebuffer", summary);
    VV_LOG_INFO("gpu: framebuffer %s", summary);
    vv::publishLog(this);
    return true;
}

void VirtioGpuDevice::probeContiguousAllocation()
{
#if VV_DEBUG
    // T-01 확인: 최대 모드(2560×1600, 약 16 MiB)의 물리 연속 할당이 부팅 직후 성공하는지. 확인만 하고 바로 놓는다.
    const uint64_t bytes = vv::display::framebufferBytes(vv::display::kMaxMode);
    VVDmaBuffer *probe = VVDmaBuffer::withLength(static_cast<size_t>(bytes));
    char summary[kSummaryBytes];
    snprintf(summary, sizeof(summary), "%ux%u bytes=%llu %s", vv::display::kMaxMode.width,
             vv::display::kMaxMode.height, bytes,
             probe == nullptr ? "FAILED" : (probe->physicalAddress() != 0 ? "ok-contiguous" : "ok-not-contiguous"));
    setProperty("VVContigProbe", summary);
    VV_LOG_INFO("gpu: contiguous probe %s", summary);
    if (probe != nullptr) {
        probe->release();
    }
#endif
}

void VirtioGpuDevice::failDevice(const char *reason)
{
    VV_LOG_ERR("gpu: init failed: %s", reason);
    setProperty("VVInitResult", reason);
    if (mTransport != nullptr) {
        // §3.1.1: 회복할 수 없으면 FAILED를 쓴다.
        mTransport->addStatus(v::status::kFailed);
    }
    releaseDevice();
    vv::publishLog(this);
}

void VirtioGpuDevice::releaseDevice()
{
    stopTimers();
    // 장치가 큐 메모리를 더 쓰지 않도록 reset을 먼저 한다. reset 뒤에는 인터럽트도 오지 않는다.
    if (mTransport != nullptr) {
        mTransport->reset();
    }
    if (mWorkLoop != nullptr) {
        releaseInterrupts();
    }
    releaseQueues();
    if (mTransport != nullptr) {
        mTransport->release();
        mTransport = nullptr;
    }
    if (mGate != nullptr) {
        if (mWorkLoop != nullptr) {
            mWorkLoop->removeEventSource(mGate);
        }
        mGate->release();
        mGate = nullptr;
    }
    // 장치를 reset한 뒤라 프레임버퍼·명령 버퍼를 더는 쓰지 않는다.
    if (mFramebuffer != nullptr) {
        mFramebuffer->release();
        mFramebuffer = nullptr;
    }
    if (mCommandBuffer != nullptr) {
        mCommandBuffer->release();
        mCommandBuffer = nullptr;
    }
}

void VirtioGpuDevice::publishPciDiagnostics(IOPCIDevice *pci)
{
    const uint16_t command = pci->configRead16(kIOPCIConfigCommand);
    auto read8 = [pci](uint8_t offset) { return pci->configRead8(offset); };
    const uint8_t msix = vv::pci::findCapability(read8, vv::pci::kCapIdMsix);
    const uint16_t msixControl =
        msix != 0 ? pci->configRead16(static_cast<uint8_t>(msix + vv::pci::kMsixControlOffset)) : 0;
    setProperty("VVPciCommand", command, 16);
    setProperty("VVMsixControl", msixControl, 16);
    VV_LOG_INFO("gpu: pci cmd=0x%04x msix cap=0x%02x control=0x%04x", command, msix, msixControl);
}

void VirtioGpuDevice::startTimers()
{
    mStatusTimer = IOTimerEventSource::timerEventSource(this, &VirtioGpuDevice::statusTimerFired);
    if (mStatusTimer == nullptr || mWorkLoop->addEventSource(mStatusTimer) != kIOReturnSuccess) {
        VV_LOG_ERR("gpu: status timer setup failed");
    } else {
        mStatusTimer->setTimeoutMS(kStatusWatchIntervalMs);
    }
    if (testResetRequested()) {
        mTestResetTimer = IOTimerEventSource::timerEventSource(this, &VirtioGpuDevice::testResetFired);
        if (mTestResetTimer != nullptr && mWorkLoop->addEventSource(mTestResetTimer) == kIOReturnSuccess) {
            mTestResetTimer->setTimeoutMS(kTestResetDelayMs);
            VV_LOG_INFO("gpu: test reset scheduled in %u ms", kTestResetDelayMs);
        }
    }
    if (bootArgEnabled(kTestPatternBootArg)) {
        mPatternTimer = IOTimerEventSource::timerEventSource(this, &VirtioGpuDevice::patternTimerFired);
        if (mPatternTimer != nullptr && mWorkLoop->addEventSource(mPatternTimer) == kIOReturnSuccess) {
            mPatternTimer->setTimeoutMS(kPatternStartDelayMs);
            setProperty("VVPatternTest", "scheduled");
            VV_LOG_INFO("gpu: pattern test scheduled in %u ms", kPatternStartDelayMs);
        }
    }
    if (bootArgEnabled(kTakeoverBootArg)) {
        mTakeoverTimer = IOTimerEventSource::timerEventSource(this, &VirtioGpuDevice::takeoverTimerFired);
        if (mTakeoverTimer != nullptr && mWorkLoop->addEventSource(mTakeoverTimer) == kIOReturnSuccess) {
            mTakeoverTimer->setTimeoutMS(kTakeoverDelayMs);
            setProperty("VVTakeover", "scheduled");
            VV_LOG_INFO("gpu: takeover scheduled in %u ms", kTakeoverDelayMs);
        }
    }
    if (mIrqMode != v::IrqMode::Poll) {
        return;
    }
    mPollTimer = IOTimerEventSource::timerEventSource(this, &VirtioGpuDevice::pollTimerFired);
    if (mPollTimer == nullptr || mWorkLoop->addEventSource(mPollTimer) != kIOReturnSuccess) {
        VV_LOG_ERR("gpu: poll timer setup failed");
        return;
    }
    mPollTimer->setTimeoutMS(kPollIntervalMs);
}

void VirtioGpuDevice::stopTimers()
{
    IOTimerEventSource **timers[] = {&mStatusTimer, &mPollTimer, &mTestResetTimer, &mPatternTimer, &mTakeoverTimer};
    for (IOTimerEventSource **timer : timers) {
        if (*timer == nullptr) {
            continue;
        }
        (*timer)->cancelTimeout();
        mWorkLoop->removeEventSource(*timer);
        (*timer)->release();
        *timer = nullptr;
    }
}

void VirtioGpuDevice::statusTimerFired(OSObject *owner, IOTimerEventSource *sender)
{
    auto *self = OSDynamicCast(VirtioGpuDevice, owner);
    if (self == nullptr) {
        return;
    }
    self->checkStatus();
    self->publishCounters();
    sender->setTimeoutMS(kStatusWatchIntervalMs);
}

void VirtioGpuDevice::testResetFired(OSObject *owner, IOTimerEventSource *)
{
    // 외부 reset을 흉내 낸다: 드라이버 상태를 그대로 둔 채 장치만 reset한다. 상태 감시가 이를 Lost로 보고 복구해야 한다.
    auto *self = OSDynamicCast(VirtioGpuDevice, owner);
    if (self == nullptr || self->mTransport == nullptr || self->mGivenUp) {
        return;
    }
    VV_LOG_INFO("gpu: test: simulating external device reset");
    self->setProperty("VVTestResetDone", true);
    self->mTransport->reset();
}

void VirtioGpuDevice::checkStatus()
{
    if (mTransport == nullptr) {
        return;
    }
    const uint8_t status = mTransport->status();
    ++mStatusChecks;
    if (status != mExpectedStatus && !mGivenUp) {
        ++mStatusMismatches;
        VV_LOG_ERR("gpu: device status changed externally 0x%02x -> 0x%02x", mExpectedStatus, status);
        mExpectedStatus = status;
        const auto health = v::classifyStatus(status);
        if (health != v::DeviceHealth::Ok) {
            recoverDevice(v::deviceHealthName(health));
        }
        vv::publishLog(this);
    }
    setProperty("VVDeviceStatus", status, 8);
    setProperty("VVStatusChecks", mStatusChecks, 32);
    setProperty("VVStatusMismatches", mStatusMismatches, 32);

    // 인터럽트가 오지 않을 때 원인을 가르기 위한 현재 값 (WI-1.005).
    // events_read가 남아 있으면 장치는 이벤트를 냈는데 인터럽트가 처리되지 않은 것이다.
    using Gpu = v::gpu::Config;
    setProperty("VVGpuEventsReadNow", mTransport->deviceRead32(offsetof(Gpu, eventsRead)), 32);
    if (mPci != nullptr) {
        auto read8 = [this](uint8_t offset) { return mPci->configRead8(offset); };
        const uint8_t msix = vv::pci::findCapability(read8, vv::pci::kCapIdMsix);
        if (msix != 0) {
            setProperty("VVMsixControlNow",
                        mPci->configRead16(static_cast<uint8_t>(msix + vv::pci::kMsixControlOffset)), 16);
        }
        setProperty("VVPciCommandNow", mPci->configRead16(kIOPCIConfigCommand), 16);
    }
}

void VirtioGpuDevice::publishCounters()
{
    char summary[kSummaryBytes];
    snprintf(summary, sizeof(summary),
             "interrupts=%u config=%u controlq=%u cursorq=%u completions=%u/%u device-errors=%u commands=%u "
             "timeouts=%u errors=%u recoveries=%u",
             mInterrupts, mConfigEvents, mQueueEvents[0], mQueueEvents[1], mCompletions[0], mCompletions[1],
             mDeviceErrors, mCommandsSent, mCommandTimeouts, mCommandErrors, mRecoveries);
    setProperty("VVEventCounters", summary);
}

void VirtioGpuDevice::stop(IOService *provider)
{
    mStopping = true;
    mScanoutRequested = false;  // 전송 루프를 멈춘다
    // 대기 중인 thread_call은 취소하고, 이미 실행 중이면 끝날 때까지 기다린다 (mStopping을 보고 빨리 끝난다).
    // (thread_call_cancel_wait는 com.apple.kpi.private라 서드파티 kext가 쓸 수 없다.)
    thread_call_t *calls[] = {&mDisplayQueryCall, &mPatternCall, &mFlushCall, &mTakeoverCall};
    for (thread_call_t *call : calls) {
        if (*call != nullptr) {
            thread_call_cancel(*call);
        }
    }
    for (uint32_t waited = 0; mDisplayQueryActive != 0 && waited < kDisplayQueryDrainMs; waited += kDrainPollMs) {
        IOSleep(kDrainPollMs);
    }
    for (thread_call_t *call : calls) {
        if (*call == nullptr) {
            continue;
        }
        if (mDisplayQueryActive != 0 || !thread_call_free(*call)) {
            // 해제하지 못하면 메모리를 남기는 쪽을 택한다 (실행 중인 호출이 해제된 객체를 쓰는 것보다 안전).
            VV_LOG_ERR("gpu: thread call still active at stop, leaking it");
        }
        *call = nullptr;
    }
    // 장치를 다른 드라이버(또는 다음 부팅)가 처음부터 쓸 수 있게 reset해 둔다.
    releaseDevice();
    VV_LOG_INFO("gpu: stop");
    vv::publishLog(this);
    if (mWorkLoop != nullptr) {
        mWorkLoop->release();
        mWorkLoop = nullptr;
    }
    if (mPci != nullptr) {
        mPci->release();
        mPci = nullptr;
    }
    IOService::stop(provider);
}
