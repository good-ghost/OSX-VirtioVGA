// 하드웨어 커서 (WI-4.001, REQ-033).
//
// 이미지: 64×64 BGRA resource 하나를 만들어 두고(controlq, 기다림), 새 이미지마다 backing을 채워 TRANSFER_TO_HOST_2D
// 한 뒤 cursorq에 UPDATE_CURSOR를 올린다. 위치: cursorq에 MOVE_CURSOR를 올리고 응답을 기다리지 않는다.
// 커서는 자주 움직이므로 프레임버퍼 스레드가 장치 응답을 기다리면 안 된다 (PHASE-PLAN-04 WI-4.001).
// cursorq 회수는 인터럽트 처리(handleQueueEvent)와 다음 제출 때 한다.

#include "VirtioGpuDevice.hpp"
#include "VirtioGpuDevicePrivate.hpp"

#include <IOKit/IOCommandGate.h>
#include <libkern/c++/OSNumber.h>
#include <kern/debug.h>
#include <pexpert/pexpert.h>

#include "VVDmaBuffer.hpp"
#include "VVLog.hpp"
#include "VirtioPciTransport.hpp"
#include "VirtioQueue.hpp"

namespace v = vv::virtio;
namespace c = vv::cursor;
using namespace vv::gpudevice;

namespace {
constexpr uint32_t kScanoutId = 0;
// cursorq 요청 칸. 칸 번호는 디스크립터 번호이므로 큐 크기 이상이어야 한다 (QEMU cursorq는 16).
constexpr uint32_t kCursorSlotBytes = 64;
constexpr uint32_t kCursorSlotCount = 256;
static_assert(sizeof(v::gpu::UpdateCursor) <= kCursorSlotBytes, "cursor slot");
constexpr vv::display::Mode kCursorResourceSize{c::kSize, c::kSize};
// 커서 크기 비율(퍼센트, 50 또는 100). 호스트 창이 게스트 화면을 절반으로 보여 줄 때 50 (WI-4.001).
constexpr const char *kCursorScaleBootArg = "vvideo_cursor_scale";
constexpr const char *kCursorScaleProperty = "VVCursorScale";
} // namespace

uint32_t VirtioGpuDevice::cursorScaleSetting()
{
    // boot-arg가 있으면 그것을, 없으면 Info.plist personality의 VVCursorScale을 쓴다. 둘 다 없으면 100.
    // personality 값을 두는 이유: OpenCore 설정(EFI)을 고치지 않고 kext 설치만으로 기본값을 정하기 위해서다.
    uint32_t percent = c::kScaleFull;
    if (!PE_parse_boot_argn(kCursorScaleBootArg, &percent, sizeof(percent))) {
        auto *setting = OSDynamicCast(OSNumber, getProperty(kCursorScaleProperty));
        percent = setting != nullptr ? setting->unsigned32BitValue() : c::kScaleFull;
    }
    return c::normalizeScale(percent);
}

bool VirtioGpuDevice::ensureCursorResource()
{
    if (!isReadyForFramebuffer()) {
        return false;
    }
    if (mCursorResourceId != 0 && mCursorRecoveryEpoch == mRecoveries) {
        return true;
    }
    mCursorScale = cursorScaleSetting();
    if (mCursorBacking == nullptr) {
        mCursorBacking = VVDmaBuffer::withLength(c::kBytes);
    }
    if (mCursorRequests == nullptr) {
        mCursorRequests = VVDmaBuffer::withLength(kCursorSlotBytes * kCursorSlotCount);
    }
    if (mCursorBacking == nullptr || mCursorRequests == nullptr) {
        VV_LOG_ERR("gpu: cursor buffers not allocated");
        return false;
    }
    const uint32_t resourceId = allocateResourceId();
    if (createResource2d(resourceId, c::kFormat, kCursorResourceSize) != kIOReturnSuccess) {
        return false;
    }
    if (attachBacking(resourceId, mCursorBacking->deviceAddress(), c::kBytes) != kIOReturnSuccess) {
        unrefResource(resourceId);
        return false;
    }
    mCursorResourceId = resourceId;
    mCursorRecoveryEpoch = mRecoveries;
    mCursorImageReady = false;
    mCursorVisible = false;
    VV_LOG_INFO("gpu: cursor resource %u (%ux%u)", resourceId, c::kSize, c::kSize);
    vv::publishLog(this);
    return true;
}

uint8_t *VirtioGpuDevice::prepareCursorImage()
{
    if (!ensureCursorResource()) {
        return nullptr;
    }
    bzero(mCursorBacking->cpuAddress(), c::kBytes);
    return static_cast<uint8_t *>(mCursorBacking->cpuAddress());
}

IOReturn VirtioGpuDevice::commitCursorImage(uint32_t width, uint32_t height, uint32_t hotX, uint32_t hotY)
{
    if (mCursorResourceId == 0) {
        return kIOReturnNotReady;
    }
    if (!c::fits(width, height, hotX, hotY)) {
        ++mCursorRejects;
        VV_LOG_ERR("gpu: cursor %ux%u hot %u,%u does not fit %ux%u", width, height, hotX, hotY, c::kSize, c::kSize);
        hideCursor();
        return kIOReturnUnsupported;
    }
    // IOGraphics는 행을 커서 폭으로 붙여 쓰므로 resource 행 간격(64픽셀)으로 다시 놓는다 (0.0.38 실측).
    auto *pixels = static_cast<uint8_t *>(mCursorBacking->cpuAddress());
    c::repackRows(pixels, width, height);
    c::Image sent{width, height, hotX, hotY};
    if (mCursorScale == c::kScaleHalf) {
        sent = c::halve(pixels, sent);
    }
    const IOReturn transferred = transferToHost(mCursorResourceId, kCursorResourceSize);
    if (transferred != kIOReturnSuccess) {
        hideCursor();
        return transferred;
    }
    const bool firstImage = mCursorImages == 0;
    // 포인터 위치는 원래 이미지 기준(IOGraphics가 준 왼쪽 위 + 원래 핫스팟)으로 구하고, 장치에는 보낸 이미지의
    // 핫스팟을 준다. 줄인 경우에도 포인터 끝이 같은 화면 점에 놓인다.
    mCursorHotX = hotX;
    mCursorHotY = hotY;
    mCursorDeviceHotX = sent.hotX;
    mCursorDeviceHotY = sent.hotY;
    mCursorImageReady = true;
    ++mCursorImages;
    if (firstImage) {
        VV_LOG_INFO("gpu: first cursor image %ux%u hot %u,%u scale %u%% -> %ux%u", width, height, hotX, hotY,
                    mCursorScale, sent.width, sent.height);
        vv::publishLog(this);
    }
    if (mCursorVisible) {
        // 이미지가 바뀌었음을 장치에 알린다. 위치는 장치가 기억하는 마지막 값을 그대로 쓴다(pos는 무시되지 않으므로
        // 다음 setCursorState가 곧 바로잡는다).
        queueCursorCommand(c::makeCommand(v::gpu::CtrlType::CmdUpdateCursor, kScanoutId, mCursorResourceId,
                                          mCursorPosition, mCursorDeviceHotX, mCursorDeviceHotY));
    }
    return kIOReturnSuccess;
}

void VirtioGpuDevice::setCursorState(int32_t left, int32_t top, bool visible)
{
    if (!mCursorImageReady) {
        return;
    }
    const c::Position position = c::pointerPosition(left, top, mCursorHotX, mCursorHotY);
    if (visible) {
        // 숨겨져 있었으면 이미지와 함께 보이게 하고(UPDATE), 보이는 중이면 위치만 옮긴다(MOVE).
        const auto type = mCursorVisible ? v::gpu::CtrlType::CmdMoveCursor : v::gpu::CtrlType::CmdUpdateCursor;
        mCursorVisible = true;
        mCursorPosition = position;
        ++mCursorMoves;
        queueCursorCommand(
            c::makeCommand(type, kScanoutId, mCursorResourceId, position, mCursorDeviceHotX, mCursorDeviceHotY));
    } else if (mCursorVisible) {
        mCursorPosition = position;
        hideCursor();
    }
}

void VirtioGpuDevice::hideCursor()
{
    if (!mCursorVisible) {
        return;
    }
    mCursorVisible = false;
    ++mCursorHides;
    // resource 0으로 UPDATE_CURSOR를 보내면 장치가 커서를 숨긴다.
    queueCursorCommand(c::makeCommand(v::gpu::CtrlType::CmdUpdateCursor, kScanoutId, 0, mCursorPosition, 0, 0));
}

void VirtioGpuDevice::queueCursorCommand(const v::gpu::UpdateCursor &request)
{
    // 게이트는 스레드 문맥에서만 잡을 수 있다. 인터럽트 문맥에서 오면 이번 이동은 버린다(다음 이동이 바로잡는다).
    if (mGate == nullptr || ml_at_interrupt_context()) {
        ++mCursorDrops;
        return;
    }
    if (mGate->runAction(&VirtioGpuDevice::gatedCursorSubmit, const_cast<v::gpu::UpdateCursor *>(&request)) !=
        kIOReturnSuccess) {
        ++mCursorDrops;
    }
}

IOReturn VirtioGpuDevice::gatedCursorSubmit(OSObject *owner, void *request, void *, void *, void *)
{
    auto *self = OSDynamicCast(VirtioGpuDevice, owner);
    if (self == nullptr || self->mGivenUp || self->mTransport == nullptr || self->mCursorRequests == nullptr) {
        return kIOReturnNotReady;
    }
    VirtioQueue *queue = self->mQueues[v::gpu::kCursorQueue];
    if (queue == nullptr) {
        return kIOReturnNotReady;
    }
    // 끝난 요청을 먼저 회수해 디스크립터를 비운다. 장치 오류(범위 밖 id)는 인터럽트 처리가 복구한다.
    v::RingCompletion completion{};
    while (queue->ring().collect(completion) == v::SplitRing::CollectResult::Completed) {
        ++self->mCompletions[v::gpu::kCursorQueue];
    }
    const uint16_t slot = queue->ring().nextFreeHead();
    if (queue->ring().freeCount() == 0 || slot >= kCursorSlotCount) {
        return kIOReturnNoResources;
    }
    // 링에 올리기 전에 칸을 채운다. 장치가 쓰다 만 요청을 읽지 않게 하기 위해서다.
    auto *slots = static_cast<uint8_t *>(self->mCursorRequests->cpuAddress());
    bcopy(request, slots + slot * kCursorSlotBytes, sizeof(v::gpu::UpdateCursor));
    const v::RingSegment segment{self->mCursorRequests->deviceAddress() + slot * kCursorSlotBytes,
                                 sizeof(v::gpu::UpdateCursor), false};
    uint16_t head = 0;
    if (!queue->ring().add(&segment, 1, head)) {
        return kIOReturnNoResources;
    }
    self->mTransport->notifyQueue(queue);
    return kIOReturnSuccess;
}

void VirtioGpuDevice::publishCursor()
{
    char summary[kSummaryBytes];
    snprintf(summary, sizeof(summary),
             "resource=%u images=%u moves=%u hides=%u drops=%u rejects=%u visible=%d hot=%u,%u scale=%u",
             mCursorResourceId, mCursorImages, mCursorMoves, mCursorHides, mCursorDrops, mCursorRejects,
             mCursorVisible ? 1 : 0, mCursorHotX, mCursorHotY, mCursorScale);
    setProperty("VVCursor", summary);
}

void VirtioGpuDevice::releaseCursor()
{
    // 장치를 reset한 뒤에 부른다. resource는 장치와 함께 사라졌다.
    mCursorResourceId = 0;
    mCursorImageReady = false;
    mCursorVisible = false;
    if (mCursorBacking != nullptr) {
        mCursorBacking->release();
        mCursorBacking = nullptr;
    }
    if (mCursorRequests != nullptr) {
        mCursorRequests->release();
        mCursorRequests = nullptr;
    }
}
