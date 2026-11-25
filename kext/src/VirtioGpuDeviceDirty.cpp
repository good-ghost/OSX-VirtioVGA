// 변경 감지 전송 (WI-4.004, NFR-001).
//
// 주기 전송 루프가 매 주기 flushFrame을 부른다. 꺼져 있으면 지금까지처럼 화면 전체를 TRANSFER·FLUSH 한다.
// 켜져 있으면 shadow와 비교해 바뀐 사각형만 TRANSFER하고 그것들을 덮는 사각형 하나를 FLUSH 한다. 바뀐 것이
// 없으면 아무 명령도 보내지 않는다. shadow는 전송 루프 스레드만 만지고 지운다(경쟁 없음).
//
// 설정: Info.plist personality `VVDirtyFlush`(0/1)·`VVDirtyTile`(16~256의 2의 거듭제곱) > boot-arg
// `vvideo_dirty`·`vvideo_dirty_tile`이 덮어씀. 실행 중에는 root 권한으로 같은 이름의 속성을 설정해 바꾼다
// (측정용, scripts/vvctl.sh).

#include "VirtioGpuDevice.hpp"
#include "VirtioGpuDevicePrivate.hpp"

#include <IOKit/IOLib.h>
#include <IOKit/IOUserClient.h>
#include <kern/clock.h>
#include <libkern/c++/OSBoolean.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <pexpert/pexpert.h>

#include "VVDmaBuffer.hpp"
#include "VVLog.hpp"
#include "vv/DirtyTiles.hpp"

namespace v = vv::virtio;
namespace d = vv::dirty;
using namespace vv::gpudevice;

namespace {
constexpr const char *kDirtyProperty = "VVDirtyFlush";
constexpr const char *kDirtyTileProperty = "VVDirtyTile";
constexpr const char *kDirtyBootArg = "vvideo_dirty";
constexpr const char *kDirtyTileBootArg = "vvideo_dirty_tile";
// 한 주기에 보내는 TRANSFER 수 한도. 넘으면 전체를 덮는 사각형 하나로 보낸다 (명령 왕복 수 제한).
constexpr size_t kMaxDirtyRects = 16;
constexpr uint64_t kBytesPerMiB = 1024 * 1024;

bool readNumber(OSObject *object, uint32_t *out)
{
    if (auto *number = OSDynamicCast(OSNumber, object)) {
        *out = number->unsigned32BitValue();
        return true;
    }
    if (auto *boolean = OSDynamicCast(OSBoolean, object)) {
        *out = boolean->isTrue() ? 1 : 0;
        return true;
    }
    return false;
}
} // namespace

void VirtioGpuDevice::loadDirtySettings()
{
    uint32_t enabled = 0;
    uint32_t tile = d::kDefaultTile;
    readNumber(getProperty(kDirtyProperty), &enabled);
    readNumber(getProperty(kDirtyTileProperty), &tile);
    uint32_t value = 0;
    if (PE_parse_boot_argn(kDirtyBootArg, &value, sizeof(value))) {
        enabled = value;
    }
    if (PE_parse_boot_argn(kDirtyTileBootArg, &value, sizeof(value))) {
        tile = value;
    }
    mDirtyEnabled = enabled != 0;
    mDirtyTile = d::normalizeTile(tile);
    mShadowValid = false;
}

IOReturn VirtioGpuDevice::setProperties(OSObject *properties)
{
    // 측정 때 재부팅 없이 켜고 끄기 위한 입구. 아무 프로세스나 화면 전송 방식을 바꾸지 못하게 막는다.
    // kIOClientPrivilegeAdministrator는 이 macOS에서 root만 통과한다 (관리자 계정은 0xe00002c1, 실측).
    if (IOUserClient::clientHasPrivilege(current_task(), kIOClientPrivilegeAdministrator) != kIOReturnSuccess) {
        return kIOReturnNotPrivileged;
    }
    auto *dict = OSDynamicCast(OSDictionary, properties);
    if (dict == nullptr) {
        return kIOReturnBadArgument;
    }
    uint32_t enabled = mDirtyEnabled ? 1 : 0;
    uint32_t tile = mDirtyTile;
    bool known = false;
    if (OSObject *object = dict->getObject(kDirtyProperty)) {
        if (!readNumber(object, &enabled) || enabled > 1) {
            return kIOReturnBadArgument;
        }
        known = true;
    }
    if (OSObject *object = dict->getObject(kDirtyTileProperty)) {
        if (!readNumber(object, &tile) || d::normalizeTile(tile) != tile) {
            return kIOReturnBadArgument;
        }
        known = true;
    }
    if (!known) {
        return kIOReturnUnsupported;
    }
    mDirtyTile = tile;
    mDirtyEnabled = enabled != 0;
    mShadowValid = false;  // 전송 루프가 다음 주기에 전체를 보내고 shadow를 다시 맞춘다
    setProperty(kDirtyProperty, mDirtyEnabled ? 1ULL : 0ULL, 32);
    setProperty(kDirtyTileProperty, mDirtyTile, 32);
    VV_LOG_INFO("gpu: dirty flush %s tile %u", mDirtyEnabled ? "on" : "off", mDirtyTile);
    vv::publishLog(this);
    return kIOReturnSuccess;
}

bool VirtioGpuDevice::ensureShadow(const vv::display::Mode &mode)
{
    const size_t bytes = static_cast<size_t>(vv::display::framebufferBytes(mode));
    if (mShadow != nullptr && mShadowBytes == bytes) {
        return true;
    }
    releaseShadow();
    mShadow = static_cast<uint8_t *>(IOMallocData(bytes));
    if (mShadow == nullptr) {
        VV_LOG_ERR("gpu: shadow %lu bytes not allocated, dirty flush off", static_cast<unsigned long>(bytes));
        return false;
    }
    mShadowBytes = bytes;
    mShadowValid = false;
    return true;
}

void VirtioGpuDevice::releaseShadow()
{
    if (mShadow != nullptr) {
        IOFreeData(mShadow, mShadowBytes);
        mShadow = nullptr;
        mShadowBytes = 0;
    }
    mShadowValid = false;
}

bool VirtioGpuDevice::flushFrame(const vv::display::Mode &mode)
{
    const uint64_t frameBytes = vv::display::framebufferBytes(mode);
    mFullEquivalentBytes += frameBytes;
    const bool dirty = mDirtyEnabled && ensureShadow(mode);
    if (!dirty || !mShadowValid) {
        if (dirty) {
            // 기준을 다시 잡는다: 지금 화면을 shadow에 복사하고 전체를 보낸다.
            bcopy(mFramebuffer->cpuAddress(), mShadow, static_cast<size_t>(frameBytes));
            mShadowValid = true;
        }
        if (transferToHost(mScanoutResourceId, mode) != kIOReturnSuccess ||
            flushResource(mScanoutResourceId, mode) != kIOReturnSuccess) {
            return false;
        }
        ++mFullFrames;
        mSentBytes += frameBytes;
        return true;
    }
    const uint32_t pitch = mode.width * d::kBytesPerPixel;
    d::Rect rects[kMaxDirtyRects];
    const uint64_t scanStart = mach_absolute_time();
    const size_t count = d::scan(static_cast<const uint8_t *>(mFramebuffer->cpuAddress()), mShadow, mode.width,
                                 mode.height, pitch, mDirtyTile, rects, kMaxDirtyRects);
    mScanTicks += mach_absolute_time() - scanStart;
    ++mScans;
    if (count == 0) {
        ++mSkippedFrames;
        return true;
    }
    for (size_t i = 0; i < count; ++i) {
        if (transferRect(mScanoutResourceId, rects[i], pitch) != kIOReturnSuccess) {
            mShadowValid = false;
            return false;
        }
        mSentBytes += static_cast<uint64_t>(rects[i].width) * rects[i].height * d::kBytesPerPixel;
    }
    if (flushRect(mScanoutResourceId, d::bounds(rects, count)) != kIOReturnSuccess) {
        mShadowValid = false;
        return false;
    }
    ++mPartialFrames;
    return true;
}

IOReturn VirtioGpuDevice::transferRect(uint32_t resourceId, const vv::dirty::Rect &rect, uint32_t pitch)
{
    v::gpu::TransferToHost2d request{};
    request.hdr.type = static_cast<uint32_t>(v::gpu::CtrlType::CmdTransferToHost2d);
    request.r = v::gpu::Rect{rect.x, rect.y, rect.width, rect.height};
    request.offset = d::backingOffset(rect, pitch);
    request.resourceId = resourceId;
    return sendNodataCommand(&request, sizeof(request), resourceId);
}

IOReturn VirtioGpuDevice::flushRect(uint32_t resourceId, const vv::dirty::Rect &rect)
{
    v::gpu::ResourceFlush request{};
    request.hdr.type = static_cast<uint32_t>(v::gpu::CtrlType::CmdResourceFlush);
    request.r = v::gpu::Rect{rect.x, rect.y, rect.width, rect.height};
    request.resourceId = resourceId;
    return sendNodataCommand(&request, sizeof(request), resourceId);
}

void VirtioGpuDevice::publishDirtyStats()
{
    uint64_t scanNs = 0;
    absolutetime_to_nanoseconds(mScanTicks, &scanNs);
    const uint64_t scanUsAvg = mScans != 0 ? scanNs / 1000 / mScans : 0;
    char summary[kSummaryBytes];
    snprintf(summary, sizeof(summary),
             "on=%d tile=%u full=%u partial=%u skipped=%u sent-mib=%llu full-mib=%llu scan-us-avg=%llu",
             mDirtyEnabled ? 1 : 0, mDirtyTile, mFullFrames, mPartialFrames, mSkippedFrames,
             mSentBytes / kBytesPerMiB, mFullEquivalentBytes / kBytesPerMiB, scanUsAvg);
    setProperty("VVDirtyStats", summary);
}
