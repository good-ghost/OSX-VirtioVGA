// virtio-gpu 정의 (WI-0.008). 이 드라이버가 쓰는 2D·커서 부분만 둔다 (3D/blob은 REQ-027로 범위 밖).
//
// 출처 (NFR-012: Linux·QEMU 헤더를 복사하지 않고 사양 원문에서 옮겼다):
//   VIRTIO 사양 §5.7 GPU Device. 1.2 cs01 HTML 읽기가 중간에 잘려, 사양 LaTeX 소스 master에서 옮겼다:
//   https://raw.githubusercontent.com/oasis-tcs/virtio-spec/master/device-types/gpu/description.tex
//   master는 1.2 이후 내용(BLOB_ALIGNMENT feature, config의 blob_alignment 필드)을 포함한다. 이 드라이버는
//   그 기능을 협상하지 않으므로 해당 필드는 두지 않았다.
// enum 값은 사양이 각 그룹의 첫 값만 적고 나머지는 C enum 순서로 정해진다. 아래 값은 그 순서대로 계산했다.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "vv/VirtioSpec.hpp"

namespace vv::virtio::gpu {

// §5.7.1 Device ID
inline constexpr uint16_t kDeviceId = 16;
inline constexpr uint16_t kPciDeviceId = pci::kModernDeviceIdBase + kDeviceId;  // 0x1050
static_assert(kPciDeviceId == 0x1050, "§4.1.2 + §5.7.1");

// §5.7.2 Virtqueues
inline constexpr uint16_t kControlQueue = 0;
inline constexpr uint16_t kCursorQueue = 1;

// §5.7.3 Feature bits
namespace feature {
inline constexpr unsigned kVirgl = 0;
inline constexpr unsigned kEdid = 1;
inline constexpr unsigned kResourceUuid = 2;
inline constexpr unsigned kResourceBlob = 3;
inline constexpr unsigned kContextInit = 4;
} // namespace feature

// §5.7.4 Device configuration layout
inline constexpr uint32_t kEventDisplay = 1u << 0;
inline constexpr uint32_t kMaxScanouts = 16;

struct Config {
    uint32_t eventsRead;   // 드라이버는 쓰지 않는다
    uint32_t eventsClear;  // 1을 쓰면 eventsRead의 해당 비트가 지워진다
    uint32_t numScanouts;  // 1 ~ 16
    uint32_t numCapsets;
};
static_assert(sizeof(Config) == 16, "§5.7.4 (blob_alignment 제외)");

// §5.7.6 enum virtio_gpu_ctrl_type
enum class CtrlType : uint32_t {
    // 2D 명령
    CmdGetDisplayInfo = 0x0100,
    CmdResourceCreate2d = 0x0101,
    CmdResourceUnref = 0x0102,
    CmdSetScanout = 0x0103,
    CmdResourceFlush = 0x0104,
    CmdTransferToHost2d = 0x0105,
    CmdResourceAttachBacking = 0x0106,
    CmdResourceDetachBacking = 0x0107,
    CmdGetCapsetInfo = 0x0108,
    CmdGetCapset = 0x0109,
    CmdGetEdid = 0x010A,
    // 커서 명령 (cursorq)
    CmdUpdateCursor = 0x0300,
    CmdMoveCursor = 0x0301,
    // 성공 응답
    RespOkNodata = 0x1100,
    RespOkDisplayInfo = 0x1101,
    RespOkCapsetInfo = 0x1102,
    RespOkCapset = 0x1103,
    RespOkEdid = 0x1104,
    // 오류 응답
    RespErrUnspec = 0x1200,
    RespErrOutOfMemory = 0x1201,
    RespErrInvalidScanoutId = 0x1202,
    RespErrInvalidResourceId = 0x1203,
    RespErrInvalidContextId = 0x1204,
    RespErrInvalidParameter = 0x1205,
};

constexpr bool isOkResponse(uint32_t type)
{
    return type >= static_cast<uint32_t>(CtrlType::RespOkNodata) &&
           type < static_cast<uint32_t>(CtrlType::RespErrUnspec);
}

constexpr bool isErrorResponse(uint32_t type)
{
    return type >= static_cast<uint32_t>(CtrlType::RespErrUnspec) &&
           type <= static_cast<uint32_t>(CtrlType::RespErrInvalidParameter);
}

inline constexpr uint32_t kFlagFence = 1u << 0;
inline constexpr uint32_t kFlagInfoRingIdx = 1u << 1;

// §5.7.6 struct virtio_gpu_ctrl_hdr
struct CtrlHdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fenceId;
    uint32_t ctxId;
    uint8_t ringIdx;
    uint8_t padding[3];
};
static_assert(sizeof(CtrlHdr) == 24, "§5.7.6");

struct Rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};
static_assert(sizeof(Rect) == 16, "§5.7.6");

// VIRTIO_GPU_CMD_GET_DISPLAY_INFO: 요청은 CtrlHdr만, 응답은 RespDisplayInfo
struct DisplayOne {
    Rect r;
    uint32_t enabled;
    uint32_t flags;
};
static_assert(sizeof(DisplayOne) == 24, "§5.7.6");

struct RespDisplayInfo {
    CtrlHdr hdr;
    DisplayOne pmodes[kMaxScanouts];
};
static_assert(sizeof(RespDisplayInfo) == 24 + 24 * 16, "§5.7.6");

// VIRTIO_GPU_CMD_GET_EDID (feature::kEdid)
inline constexpr size_t kMaxEdidBytes = 1024;

struct GetEdid {
    CtrlHdr hdr;
    uint32_t scanout;
    uint32_t padding;
};
static_assert(sizeof(GetEdid) == 32, "§5.7.6");

struct RespEdid {
    CtrlHdr hdr;
    uint32_t size;
    uint32_t padding;
    uint8_t edid[kMaxEdidBytes];
};
static_assert(sizeof(RespEdid) == 32 + kMaxEdidBytes, "§5.7.6");

// enum virtio_gpu_formats
enum class Format : uint32_t {
    B8G8R8A8Unorm = 1,
    B8G8R8X8Unorm = 2,
    A8R8G8B8Unorm = 3,
    X8R8G8B8Unorm = 4,
    R8G8B8A8Unorm = 67,
    X8B8G8R8Unorm = 68,
    A8B8G8R8Unorm = 121,
    R8G8B8X8Unorm = 134,
};

struct ResourceCreate2d {
    CtrlHdr hdr;
    uint32_t resourceId;
    uint32_t format;
    uint32_t width;
    uint32_t height;
};
static_assert(sizeof(ResourceCreate2d) == 40, "§5.7.6");

struct ResourceUnref {
    CtrlHdr hdr;
    uint32_t resourceId;
    uint32_t padding;
};
static_assert(sizeof(ResourceUnref) == 32, "§5.7.6");

struct SetScanout {
    CtrlHdr hdr;
    Rect r;
    uint32_t scanoutId;
    uint32_t resourceId;  // 0이면 scanout을 끈다
};
static_assert(sizeof(SetScanout) == 48, "§5.7.6");

struct ResourceFlush {
    CtrlHdr hdr;
    Rect r;
    uint32_t resourceId;
    uint32_t padding;
};
static_assert(sizeof(ResourceFlush) == 48, "§5.7.6");

struct TransferToHost2d {
    CtrlHdr hdr;
    Rect r;
    uint64_t offset;
    uint32_t resourceId;
    uint32_t padding;
};
static_assert(sizeof(TransferToHost2d) == 56, "§5.7.6");
static_assert(offsetof(TransferToHost2d, offset) == 40, "§5.7.6");

// RESOURCE_ATTACH_BACKING 뒤에 MemEntry가 nrEntries개 이어진다
struct ResourceAttachBacking {
    CtrlHdr hdr;
    uint32_t resourceId;
    uint32_t nrEntries;
};
static_assert(sizeof(ResourceAttachBacking) == 32, "§5.7.6");

struct MemEntry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
};
static_assert(sizeof(MemEntry) == 16, "§5.7.6");

struct ResourceDetachBacking {
    CtrlHdr hdr;
    uint32_t resourceId;
    uint32_t padding;
};
static_assert(sizeof(ResourceDetachBacking) == 32, "§5.7.6");

// cursorq: UPDATE_CURSOR와 MOVE_CURSOR가 같은 구조체를 쓴다. 응답은 RESP_OK_NODATA.
struct CursorPos {
    uint32_t scanoutId;
    uint32_t x;
    uint32_t y;
    uint32_t padding;
};
static_assert(sizeof(CursorPos) == 16, "§5.7.6");

struct UpdateCursor {
    CtrlHdr hdr;
    CursorPos pos;
    uint32_t resourceId;
    uint32_t hotX;
    uint32_t hotY;
    uint32_t padding;
};
static_assert(sizeof(UpdateCursor) == 56, "§5.7.6");

} // namespace vv::virtio::gpu
