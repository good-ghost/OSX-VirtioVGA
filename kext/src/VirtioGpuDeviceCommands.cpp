// VirtioGpuDevice의 명령 계층 (WI-1.007, WI-2.001, REQ-023, REQ-024).
// controlq에 명령을 하나씩 보내고(직렬화), 응답 길이·type을 검사해 명령 이름과 함께 결과를 남긴다.
#include "VirtioGpuDevice.hpp"
#include "VirtioGpuDevicePrivate.hpp"

#include <IOKit/IOCommandGate.h>
#include <IOKit/IOWorkLoop.h>

#include "VVDmaBuffer.hpp"
#include "VVLog.hpp"
#include "VirtioPciTransport.hpp"
#include "VirtioQueue.hpp"
#include "vv/GpuCommand.hpp"
#include "vv/MemEntries.hpp"

namespace {
namespace v = vv::virtio;
using namespace vv::gpudevice;

// 자체 시험(디버그 빌드)에서 일부러 쓰는 존재하지 않는 resource id. 우리가 만드는 id는 1부터 올라간다.
constexpr uint32_t kSelfTestInvalidResourceId = 0xFFFFFFF0;

IOReturn toIOReturn(v::gpu::CommandResult result, IOReturn transportError)
{
    using R = v::gpu::CommandResult;
    switch (result) {
    case R::Ok: return kIOReturnSuccess;
    case R::Transport: return transportError;
    case R::ErrOutOfMemory: return kIOReturnNoMemory;
    case R::ErrInvalidScanoutId:
    case R::ErrInvalidResourceId:
    case R::ErrInvalidContextId:
    case R::ErrInvalidParameter: return kIOReturnBadArgument;
    case R::TooShort:
    case R::UnexpectedType:
    case R::ErrUnspec:
    case R::ErrUnknown: return kIOReturnIOError;
    }
    return kIOReturnIOError;
}
} // namespace

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
        // 장치가 명령을 쥐고 있어 링을 더 쓸 수 없다. reset으로 되찾는다 (게이트 안이라 확인 명령은 미뤄진다).
        self->recoverDevice("command-timeout");
        return kIOReturnTimeout;
    }
    if (!self->mCommandInFlight) {
        // 기다리는 사이 복구가 일어나 명령이 버려졌다.
        return kIOReturnAborted;
    }
    self->mCommandInFlight = false;
    bcopy(buffer + kResponseOffset, response, responseBytes);
    *static_cast<uint32_t *>(written) = self->mCommandWritten;
    return kIOReturnSuccess;
}

IOReturn VirtioGpuDevice::sendCommand(const void *request, uint32_t requestBytes, void *response,
                                     uint32_t responseBytes, v::gpu::CtrlType expected, uint32_t logArg,
                                     v::gpu::CommandResult *resultOut)
{
    const uint32_t commandType = static_cast<const v::gpu::CtrlHdr *>(request)->type;
    const char *commandName = v::gpu::ctrlTypeName(commandType);
    auto finish = [resultOut](v::gpu::CommandResult result) {
        if (resultOut != nullptr) {
            *resultOut = result;
        }
    };
    if (mWorkLoop != nullptr && mWorkLoop->onThread()) {
        // 워크루프 스레드에서 기다리면 완료를 처리할 스레드가 없어 교착된다.
        VV_LOG_ERR("gpu: %s called on workloop thread, refused", commandName);
        finish(v::gpu::CommandResult::Transport);
        return kIOReturnNotPermitted;
    }
    uint32_t written = 0;
    const IOReturn submitted = submitCommand(request, requestBytes, response, responseBytes, &written);
    if (submitted != kIOReturnSuccess) {
        ++mCommandErrors;
        VV_LOG_ERR("gpu: %s arg=0x%x not completed: 0x%x", commandName, logArg, submitted);
        finish(v::gpu::CommandResult::Transport);
        return submitted;
    }
    const uint32_t responseType = static_cast<const v::gpu::CtrlHdr *>(response)->type;
    const auto check = v::gpu::checkResponse(written, responseBytes, responseType, expected);
    const auto result = v::gpu::commandResultFor(check, responseType);
    finish(result);
    if (result != v::gpu::CommandResult::Ok) {
        ++mCommandErrors;
        VV_LOG_ERR("gpu: %s arg=0x%x -> %s (%s, written %u)", commandName, logArg,
                   v::gpu::ctrlTypeName(responseType), v::gpu::commandResultName(result), written);
        return toIOReturn(result, submitted);
    }
    VV_LOG_DBG("gpu: %s arg=0x%x ok", commandName, logArg);
    return kIOReturnSuccess;
}

IOReturn VirtioGpuDevice::sendNodataCommand(const void *request, uint32_t requestBytes, uint32_t logArg,
                                           v::gpu::CommandResult *resultOut)
{
    v::gpu::CtrlHdr response{};
    return sendCommand(request, requestBytes, &response, sizeof(response), v::gpu::CtrlType::RespOkNodata, logArg,
                       resultOut);
}

void VirtioGpuDevice::queryDisplayInfo()
{
    using Gpu = v::gpu::Config;
    v::gpu::CtrlHdr request{};
    request.type = static_cast<uint32_t>(v::gpu::CtrlType::CmdGetDisplayInfo);
    v::gpu::RespDisplayInfo response{};
    v::gpu::CommandResult result = v::gpu::CommandResult::Ok;
    sendCommand(&request, sizeof(request), &response, sizeof(response), v::gpu::CtrlType::RespOkDisplayInfo, 0,
                &result);
    publishCounters();
    if (result != v::gpu::CommandResult::Ok) {
        setProperty("VVDisplayInfo", v::gpu::commandResultName(result));
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

    // WI-2.002: scanout 0의 선호 해상도. 꺼져 있거나 0이거나 상한(2560×1600)을 넘으면 1280×800을 쓴다.
    if (scanouts > 0) {
        const auto preferred = vv::display::preferredModeFrom(response.pmodes[0]);
        mPreferredMode = preferred.mode;
        char modeSummary[kSummaryBytes];
        snprintf(modeSummary, sizeof(modeSummary), "%ux%u%s", preferred.mode.width, preferred.mode.height,
                 preferred.fallbackUsed ? " (fallback)" : "");
        setProperty("VVPreferredMode", modeSummary);
        if (preferred.fallbackUsed) {
            VV_LOG_INFO("gpu: scanout 0 preferred %ux%u unusable, using fallback %ux%u", response.pmodes[0].r.width,
                        response.pmodes[0].r.height, preferred.mode.width, preferred.mode.height);
        }
    }
    vv::publishLog(this);
}

void VirtioGpuDevice::runSelfTest()
{
#if VV_DEBUG
    // 오류 응답 경로를 실제 장치로 확인한다 (WI-2.001): 없는 resource를 해제하면 ERR_INVALID_RESOURCE_ID가 와야 한다.
    // 장치 상태를 바꾸지 않는 명령이라 매 부팅 해도 된다. 릴리스 빌드에서는 하지 않는다.
    v::gpu::ResourceUnref request{};
    request.hdr.type = static_cast<uint32_t>(v::gpu::CtrlType::CmdResourceUnref);
    request.resourceId = kSelfTestInvalidResourceId;
    v::gpu::CommandResult result = v::gpu::CommandResult::Ok;
    const IOReturn ret = sendNodataCommand(&request, sizeof(request), request.resourceId, &result);
    const bool passed = result == v::gpu::CommandResult::ErrInvalidResourceId && ret == kIOReturnBadArgument;
    char summary[kSummaryBytes];
    snprintf(summary, sizeof(summary), "invalid-unref=%s ioreturn=0x%x %s", v::gpu::commandResultName(result), ret,
             passed ? "pass" : "FAIL");
    setProperty("VVSelfTest", summary);
    VV_LOG_INFO("gpu: self-test %s", summary);
    publishCounters();
    vv::publishLog(this);
#endif
}

uint32_t VirtioGpuDevice::allocateResourceId()
{
    return mNextResourceId++;
}

namespace {

template <typename Request>
Request makeRequest(v::gpu::CtrlType type)
{
    Request request{};
    request.hdr.type = static_cast<uint32_t>(type);
    return request;
}

v::gpu::Rect fullRect(const vv::display::Mode &mode)
{
    return v::gpu::Rect{0, 0, mode.width, mode.height};
}

} // namespace

IOReturn VirtioGpuDevice::createResource2d(uint32_t resourceId, v::gpu::Format format, const vv::display::Mode &mode)
{
    auto request = makeRequest<v::gpu::ResourceCreate2d>(v::gpu::CtrlType::CmdResourceCreate2d);
    request.resourceId = resourceId;
    request.format = static_cast<uint32_t>(format);
    request.width = mode.width;
    request.height = mode.height;
    return sendNodataCommand(&request, sizeof(request), resourceId);
}

IOReturn VirtioGpuDevice::attachBacking(uint32_t resourceId, uint64_t address, uint32_t length)
{
    // 프레임버퍼는 물리 연속이라 항목 하나다 (T-01). 여러 조각이 필요해지면 vv/MemEntries.hpp로 목록을 만든다.
    struct {
        v::gpu::ResourceAttachBacking header;
        v::gpu::MemEntry entries[1];
    } request{};
    request.header.hdr.type = static_cast<uint32_t>(v::gpu::CtrlType::CmdResourceAttachBacking);
    request.header.resourceId = resourceId;
    request.header.nrEntries = 1;
    const v::gpu::DmaSegment segment{address, length};
    if (v::gpu::buildMemEntries(&segment, 1, request.entries, 1) != 1) {
        return kIOReturnBadArgument;
    }
    return sendNodataCommand(&request, sizeof(request), resourceId);
}

IOReturn VirtioGpuDevice::setScanout(uint32_t scanoutId, uint32_t resourceId, const vv::display::Mode &mode)
{
    auto request = makeRequest<v::gpu::SetScanout>(v::gpu::CtrlType::CmdSetScanout);
    request.scanoutId = scanoutId;
    request.resourceId = resourceId;  // 0이면 scanout을 끈다
    request.r = resourceId == 0 ? v::gpu::Rect{} : fullRect(mode);
    return sendNodataCommand(&request, sizeof(request), resourceId);
}

IOReturn VirtioGpuDevice::transferToHost(uint32_t resourceId, const vv::display::Mode &mode)
{
    auto request = makeRequest<v::gpu::TransferToHost2d>(v::gpu::CtrlType::CmdTransferToHost2d);
    request.r = fullRect(mode);
    request.offset = 0;  // backing 시작부터 (전체 화면)
    request.resourceId = resourceId;
    return sendNodataCommand(&request, sizeof(request), resourceId);
}

IOReturn VirtioGpuDevice::flushResource(uint32_t resourceId, const vv::display::Mode &mode)
{
    auto request = makeRequest<v::gpu::ResourceFlush>(v::gpu::CtrlType::CmdResourceFlush);
    request.r = fullRect(mode);
    request.resourceId = resourceId;
    return sendNodataCommand(&request, sizeof(request), resourceId);
}

IOReturn VirtioGpuDevice::detachBacking(uint32_t resourceId)
{
    auto request = makeRequest<v::gpu::ResourceDetachBacking>(v::gpu::CtrlType::CmdResourceDetachBacking);
    request.resourceId = resourceId;
    return sendNodataCommand(&request, sizeof(request), resourceId);
}

IOReturn VirtioGpuDevice::unrefResource(uint32_t resourceId)
{
    auto request = makeRequest<v::gpu::ResourceUnref>(v::gpu::CtrlType::CmdResourceUnref);
    request.resourceId = resourceId;
    return sendNodataCommand(&request, sizeof(request), resourceId);
}

