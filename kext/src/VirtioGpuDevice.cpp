#include "VirtioGpuDevice.hpp"

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

// 상태 감시 주기. 외부(Apple 전송 드라이버 등)가 장치를 reset하는지 보기 위한 것이라 촘촘할 필요가 없다.
constexpr uint32_t kStatusWatchIntervalMs = 2000;
// Poll 방식의 확인 주기 (디버그용이라 성능보다 단순함을 택했다).
constexpr uint32_t kPollIntervalMs = 10;
// 원하는 큐 크기. 장치 최대가 더 작으면 그에 맞춘다. 명령은 한 번에 하나씩 보내므로(REQ-024) 크게 둘 필요가 없다.
constexpr uint16_t kDesiredQueueSize[] = {64, 16};  // controlq, cursorq
constexpr const char *kQueueProperty[] = {"VVControlQueue", "VVCursorQueue"};
// 인터럽트 소스 목록을 훑을 최대 인덱스 (레거시 1 + MSI-X 테이블 크기보다 넉넉히).
constexpr int kMaxInterruptSourceIndex = 16;
constexpr size_t kSummaryBytes = 128;
// 명령 응답 대기 한도 (REQ-024). QEMU는 수 ms 안에 응답하므로 넉넉히 잡는다.
constexpr uint32_t kCommandTimeoutMs = 1000;
// 명령 버퍼: 앞쪽에 요청, kResponseOffset부터 응답. 가장 큰 응답(RespEdid, 1056바이트)도 들어간다.
constexpr uint32_t kCommandBufferBytes = PAGE_SIZE;
constexpr uint32_t kResponseOffset = 1024;
constexpr uint32_t kMaxRequestBytes = kResponseOffset;
static_assert(sizeof(v::gpu::RespEdid) <= kCommandBufferBytes - kResponseOffset, "응답 영역이 가장 큰 응답보다 작다");

static_assert(v::gpu::kControlQueue == 0 && v::gpu::kCursorQueue == 1, "mQueues 배열 순서");

uint32_t high32(uint64_t value)
{
    return static_cast<uint32_t>(value >> 32);
}

uint32_t low32(uint64_t value)
{
    return static_cast<uint32_t>(value);
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
    if (initializeDevice()) {
        enableInterrupts();
        startTimers();
        queryDisplayInfo();
    }
    publishCounters();
    vv::publishLog(this);
    return true;
}

bool VirtioGpuDevice::initializeDevice()
{
    VV_LOG_INFO("gpu: status before reset=0x%02x", mTransport->status());
    // §3.1.1 1단계: reset. 펌웨어나 Apple 전송 드라이버가 남긴 상태(관찰값 ACK)를 가정하지 않는다.
    if (!mTransport->reset()) {
        failDevice("reset");
        return false;
    }
    mTransport->addStatus(v::status::kAcknowledge);
    mTransport->addStatus(v::status::kDriver);

    const uint64_t deviceFeatures = mTransport->readDeviceFeatures();
    setProperty("VVDeviceFeatures", deviceFeatures, 64);
    VV_LOG_INFO("gpu: device features hi=0x%08x lo=0x%08x", high32(deviceFeatures), low32(deviceFeatures));

    const auto negotiation = v::negotiateFeatures(deviceFeatures);
    setProperty("VVNegotiation", v::negotiationErrorName(negotiation.error));
    if (negotiation.error != v::NegotiationError::None) {
        failDevice(v::negotiationErrorName(negotiation.error));
        return false;
    }
    if (!mTransport->writeDriverFeatures(negotiation.driverFeatures)) {
        failDevice("write-features");
        return false;
    }
    const uint64_t readBack = mTransport->readDriverFeatures();
    setProperty("VVDriverFeatures", readBack, 64);
    VV_LOG_INFO("gpu: driver features hi=0x%08x lo=0x%08x (wanted lo=0x%08x)", high32(readBack), low32(readBack),
                low32(negotiation.driverFeatures));

    mTransport->addStatus(v::status::kFeaturesOk);
    // §3.1.1 6단계: FEATURES_OK를 다시 읽어 장치가 feature 조합을 받아들였는지 확인한다.
    const uint8_t status = mTransport->status();
    if ((status & v::status::kFeaturesOk) == 0) {
        failDevice("features-not-accepted");
        return false;
    }
    VV_LOG_INFO("gpu: FEATURES_OK accepted, status=0x%02x", status);

    if (!prepareInterrupts()) {
        failDevice("interrupts");
        return false;
    }
    if (!setupQueues()) {
        failDevice("queues");
        return false;
    }
    // §3.1.1 8단계: 장치별 설정(큐)을 마친 뒤 DRIVER_OK. 이 뒤로 장치는 큐를 쓸 수 있다.
    mTransport->addStatus(v::status::kDriverOk);
    const uint8_t live = mTransport->status();
    if ((live & v::status::kDriverOk) == 0 || (live & v::status::kDeviceNeedsReset) != 0) {
        failDevice("driver-ok");
        return false;
    }
    mExpectedStatus = live;
    setProperty("VVInitResult", "driver-ok");
    setProperty("VVDeviceStatus", live, 8);
    VV_LOG_INFO("gpu: DRIVER_OK, status=0x%02x irq=%s", live, v::irqModeName(mIrqMode));
    return true;
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
    // MSI-X가 아니면 장치가 ISR/INTx를 쓰도록 config 벡터를 NO_VECTOR로 둔다 (reset 직후 기본값이지만 명시한다).
    if (!mTransport->setConfigVector(v::configVectorFor(mIrqMode))) {
        VV_LOG_ERR("gpu: config vector 0x%x not accepted", v::configVectorFor(mIrqMode));
        return false;
    }
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
            break;
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

IOReturn VirtioGpuDevice::submitCommand(const void *request, uint32_t requestBytes, void *response,
                                       uint32_t responseBytes, uint32_t *written)
{
    if (mGate == nullptr || request == nullptr || response == nullptr || requestBytes == 0 ||
        requestBytes > kMaxRequestBytes || responseBytes > kCommandBufferBytes - kResponseOffset) {
        return kIOReturnBadArgument;
    }
    uint32_t lengths[2] = {requestBytes, responseBytes};
    return mGate->runAction(&VirtioGpuDevice::gatedSubmit, const_cast<void *>(request), response, lengths, written);
}

IOReturn VirtioGpuDevice::gatedSubmit(OSObject *owner, void *request, void *response, void *lengths, void *written)
{
    auto *self = OSDynamicCast(VirtioGpuDevice, owner);
    if (self == nullptr || self->mTransport == nullptr || self->mQueues[v::gpu::kControlQueue] == nullptr) {
        return kIOReturnNotReady;
    }
    if (self->mCommandInFlight) {
        // 이전 명령이 시간 초과로 장치에 남아 있다. reset(WI-1.006) 전에는 새 명령을 보내지 않는다.
        return kIOReturnBusy;
    }
    const uint32_t requestBytes = static_cast<uint32_t *>(lengths)[0];
    const uint32_t responseBytes = static_cast<uint32_t *>(lengths)[1];
    auto *buffer = static_cast<uint8_t *>(self->mCommandBuffer->cpuAddress());
    bcopy(request, buffer, requestBytes);
    bzero(buffer + kResponseOffset, responseBytes);

    const uint64_t base = self->mCommandBuffer->deviceAddress();
    const v::RingSegment segments[] = {
        {base, requestBytes, false},
        {base + kResponseOffset, responseBytes, true},
    };
    VirtioQueue *queue = self->mQueues[v::gpu::kControlQueue];
    uint16_t head = 0;
    if (!queue->ring().add(segments, 2, head)) {
        return kIOReturnNoResources;
    }
    self->mCommandHead = head;
    self->mCommandDone = false;
    self->mCommandInFlight = true;
    ++self->mCommandsSent;
    self->mTransport->notifyQueue(queue);

    AbsoluteTime deadline = 0;
    clock_interval_to_deadline(kCommandTimeoutMs, kMillisecondScale, &deadline);
    while (!self->mCommandDone) {
        // commandSleep은 깨워지면 kIOReturnSuccess, 기한이 지나면 kIOReturnTimeout을 돌려준다.
        if (self->mGate->commandSleep(&self->mCommandDone, deadline, THREAD_UNINT) == kIOReturnTimeout) {
            break;
        }
    }
    if (!self->mCommandDone) {
        ++self->mCommandTimeouts;
        VV_LOG_ERR("gpu: command timed out after %u ms (head %u)", kCommandTimeoutMs, head);
        return kIOReturnTimeout;
    }
    self->mCommandInFlight = false;
    bcopy(buffer + kResponseOffset, response, responseBytes);
    *static_cast<uint32_t *>(written) = self->mCommandWritten;
    return kIOReturnSuccess;
}

void VirtioGpuDevice::queryDisplayInfo()
{
    using Gpu = v::gpu::Config;
    v::gpu::CtrlHdr request{};
    request.type = static_cast<uint32_t>(v::gpu::CtrlType::CmdGetDisplayInfo);
    v::gpu::RespDisplayInfo response{};
    uint32_t written = 0;
    const IOReturn result = submitCommand(&request, sizeof(request), &response, sizeof(response), &written);
    publishCounters();
    if (result != kIOReturnSuccess) {
        VV_LOG_ERR("gpu: GET_DISPLAY_INFO failed: 0x%x", result);
        setProperty("VVDisplayInfo", "submit-failed");
        vv::publishLog(this);
        return;
    }
    const auto check = v::gpu::checkResponse(written, sizeof(response), response.hdr.type,
                                             v::gpu::CtrlType::RespOkDisplayInfo);
    if (check != v::gpu::ResponseError::None) {
        VV_LOG_ERR("gpu: GET_DISPLAY_INFO response %s (type 0x%x, written %u)", v::gpu::responseErrorName(check),
                   response.hdr.type, written);
        setProperty("VVDisplayInfo", v::gpu::responseErrorName(check));
        vv::publishLog(this);
        return;
    }
    const uint32_t scanouts = v::gpu::usableScanouts(mTransport->deviceRead32(offsetof(Gpu, numScanouts)));
    char summary[kSummaryBytes] = {};
    size_t used = 0;
    for (uint32_t i = 0; i < scanouts && used < sizeof(summary); ++i) {
        const auto &mode = response.pmodes[i];
        VV_LOG_INFO("gpu: scanout %u enabled=%u %ux%u+%u+%u flags=0x%x", i, mode.enabled, mode.r.width,
                    mode.r.height, mode.r.x, mode.r.y, mode.flags);
        const int n = snprintf(summary + used, sizeof(summary) - used, "%s%u:%ux%u%s", used == 0 ? "" : " ", i,
                               mode.r.width, mode.r.height, mode.enabled != 0 ? "" : "(off)");
        if (n <= 0) {
            break;
        }
        used += static_cast<size_t>(n);
    }
    setProperty("VVDisplayInfo", summary);
    vv::publishLog(this);
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
    for (VirtioQueue *&queue : mQueues) {
        if (queue != nullptr) {
            queue->release();
            queue = nullptr;
        }
    }
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
    // 장치를 reset한 뒤라 명령 버퍼를 더는 쓰지 않는다.
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
    IOTimerEventSource **timers[] = {&mStatusTimer, &mPollTimer};
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

void VirtioGpuDevice::checkStatus()
{
    if (mTransport == nullptr) {
        return;
    }
    const uint8_t status = mTransport->status();
    ++mStatusChecks;
    if (status != mExpectedStatus) {
        // 처음 바뀐 순간만 로그로 남기고, 새 값을 기준으로 다시 감시한다.
        ++mStatusMismatches;
        VV_LOG_ERR("gpu: device status changed externally 0x%02x -> 0x%02x", mExpectedStatus, status);
        mExpectedStatus = status;
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
             "timeouts=%u",
             mInterrupts, mConfigEvents, mQueueEvents[0], mQueueEvents[1], mCompletions[0], mCompletions[1],
             mDeviceErrors, mCommandsSent, mCommandTimeouts);
    setProperty("VVEventCounters", summary);
}

void VirtioGpuDevice::stop(IOService *provider)
{
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
