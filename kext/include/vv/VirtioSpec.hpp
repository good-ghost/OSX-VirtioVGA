// VIRTIO 공통 정의: 장치 상태, 예약 feature 비트, split virtqueue, PCI 전송 (WI-0.008).
//
// 출처 (NFR-012: Linux·QEMU 헤더를 복사하지 않고 사양 원문에서 옮겼다):
//   - OASIS VIRTIO 1.2 cs01 HTML: https://docs.oasis-open.org/virtio/virtio/v1.2/cs01/virtio-v1.2-cs01.html
//     §2.1 장치 상태, §2.7 split virtqueue, §4.1 PCI 전송
//   - 예약 feature 비트(§6)는 위 HTML 읽기가 중간에 잘려, 사양 LaTeX 소스 master의 content.tex에서 옮겼다:
//     https://raw.githubusercontent.com/oasis-tcs/virtio-spec/master/content.tex
// 모든 다중 바이트 필드는 리틀 엔디언(le16/le32/le64)이다. 이 드라이버는 x86_64 전용이라 호스트 바이트 순서를 그대로 쓴다.
// 커널 헤더에 의존하지 않아 사용자 공간 단위 테스트에서도 컴파일된다.
#pragma once

#include <stddef.h>
#include <stdint.h>

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "virtio 구조체를 호스트 바이트 순서로 직접 읽으므로 리틀 엔디언 전용이다");

namespace vv::virtio {

// §2.1 Device Status Field
namespace status {
inline constexpr uint8_t kAcknowledge = 1;
inline constexpr uint8_t kDriver = 2;
inline constexpr uint8_t kDriverOk = 4;
inline constexpr uint8_t kFeaturesOk = 8;
inline constexpr uint8_t kDeviceNeedsReset = 64;
inline constexpr uint8_t kFailed = 128;
} // namespace status

// §6 Reserved Feature Bits (비트 번호)
namespace feature {
inline constexpr unsigned kRingIndirectDesc = 28;
inline constexpr unsigned kRingEventIdx = 29;
inline constexpr unsigned kVersion1 = 32;
inline constexpr unsigned kAccessPlatform = 33;
inline constexpr unsigned kRingPacked = 34;
inline constexpr unsigned kInOrder = 35;
inline constexpr unsigned kOrderPlatform = 36;
inline constexpr unsigned kSrIov = 37;
inline constexpr unsigned kNotificationData = 38;
inline constexpr unsigned kNotifConfigData = 39;
inline constexpr unsigned kRingReset = 40;

constexpr uint64_t bit(unsigned featureBit)
{
    return uint64_t{1} << featureBit;
}
} // namespace feature

// §2.7.5 The Virtqueue Descriptor Table
struct VirtqDesc {
    uint64_t addr;  // guest-physical address
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};
static_assert(sizeof(VirtqDesc) == 16, "§2.7.5");

namespace descFlag {
inline constexpr uint16_t kNext = 1;
inline constexpr uint16_t kWrite = 2;
inline constexpr uint16_t kIndirect = 4;
} // namespace descFlag

// §2.7.6 The Virtqueue Available Ring: flags, idx, ring[queueSize], used_event(EVENT_IDX일 때)
struct VirtqAvailHeader {
    uint16_t flags;
    uint16_t idx;
};
static_assert(sizeof(VirtqAvailHeader) == 4, "§2.7.6");
inline constexpr uint16_t kAvailFlagNoInterrupt = 1;

// §2.7.8 The Virtqueue Used Ring: flags, idx, ring[queueSize], avail_event(EVENT_IDX일 때)
struct VirtqUsedElem {
    uint32_t id;  // 사양 주석: le32 is used here for ids for padding reasons
    uint32_t len;
};
static_assert(sizeof(VirtqUsedElem) == 8, "§2.7.8");

struct VirtqUsedHeader {
    uint16_t flags;
    uint16_t idx;
};
static_assert(sizeof(VirtqUsedHeader) == 4, "§2.7.8");
inline constexpr uint16_t kUsedFlagNoNotify = 1;

// §2.7 정렬·크기 표 (바이트)
inline constexpr size_t kDescTableAlign = 16;
inline constexpr size_t kAvailRingAlign = 2;
inline constexpr size_t kUsedRingAlign = 4;
inline constexpr uint32_t kMaxQueueSize = 32768;  // Queue Size는 2의 거듭제곱, 최대 32768

constexpr size_t descTableBytes(uint32_t queueSize)
{
    return sizeof(VirtqDesc) * queueSize;
}

// 6 = flags + idx + used_event
constexpr size_t availRingBytes(uint32_t queueSize)
{
    return 6 + sizeof(uint16_t) * queueSize;
}

// 6 = flags + idx + avail_event
constexpr size_t usedRingBytes(uint32_t queueSize)
{
    return 6 + sizeof(VirtqUsedElem) * queueSize;
}

constexpr bool isValidQueueSize(uint32_t queueSize)
{
    return queueSize != 0 && queueSize <= kMaxQueueSize && (queueSize & (queueSize - 1)) == 0;
}

// §4.1 Virtio Over PCI Bus
namespace pci {

inline constexpr uint16_t kVendorId = 0x1AF4;
// §4.1.2 non-transitional PCI Device ID = 0x1040 + Virtio Device ID
inline constexpr uint16_t kModernDeviceIdBase = 0x1040;
inline constexpr uint8_t kCapIdVendorSpecific = 0x09;  // PCI_CAP_ID_VNDR

// §4.1.4 cfg_type
namespace capType {
inline constexpr uint8_t kCommonCfg = 1;
inline constexpr uint8_t kNotifyCfg = 2;
inline constexpr uint8_t kIsrCfg = 3;
inline constexpr uint8_t kDeviceCfg = 4;
inline constexpr uint8_t kPciCfg = 5;
inline constexpr uint8_t kSharedMemoryCfg = 8;
inline constexpr uint8_t kVendorCfg = 9;
} // namespace capType

// §4.1.4 struct virtio_pci_cap (PCI 설정 공간에서 읽는다)
struct PciCap {
    uint8_t capVndr;
    uint8_t capNext;
    uint8_t capLen;
    uint8_t cfgType;
    uint8_t bar;      // 0x0 ~ 0x5
    uint8_t id;
    uint8_t padding[2];
    uint32_t offset;  // BAR 안의 시작 위치
    uint32_t length;
};
static_assert(sizeof(PciCap) == 16, "§4.1.4");
static_assert(offsetof(PciCap, offset) == 8, "§4.1.4");
inline constexpr uint8_t kMaxBarIndex = 5;

// §4.1.4.4 struct virtio_pci_notify_cap
struct PciNotifyCap {
    PciCap cap;
    uint32_t notifyOffMultiplier;  // 알림 주소 = cap.offset + queue_notify_off * notify_off_multiplier
};
static_assert(sizeof(PciNotifyCap) == 20, "§4.1.4.4");

// §4.1.4.3 struct virtio_pci_common_cfg — MMIO 레지스터 오프셋. 접근은 오프셋과 폭으로 한다.
struct PciCommonCfg {
    uint32_t deviceFeatureSelect;
    uint32_t deviceFeature;
    uint32_t driverFeatureSelect;
    uint32_t driverFeature;
    uint16_t configMsixVector;
    uint16_t numQueues;
    uint8_t deviceStatus;
    uint8_t configGeneration;
    uint16_t queueSelect;
    uint16_t queueSize;
    uint16_t queueMsixVector;
    uint16_t queueEnable;
    uint16_t queueNotifyOff;
    uint64_t queueDesc;
    uint64_t queueDriver;   // available ring
    uint64_t queueDevice;   // used ring
    uint16_t queueNotifyData;  // VIRTIO_F_NOTIF_CONFIG_DATA일 때만
    uint16_t queueReset;       // VIRTIO_F_RING_RESET일 때만
};
static_assert(offsetof(PciCommonCfg, deviceFeatureSelect) == 0, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, deviceFeature) == 4, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, driverFeatureSelect) == 8, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, driverFeature) == 12, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, configMsixVector) == 16, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, numQueues) == 18, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, deviceStatus) == 20, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, configGeneration) == 21, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, queueSelect) == 22, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, queueSize) == 24, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, queueMsixVector) == 26, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, queueEnable) == 28, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, queueNotifyOff) == 30, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, queueDesc) == 32, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, queueDriver) == 40, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, queueDevice) == 48, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, queueNotifyData) == 56, "§4.1.4.3");
static_assert(offsetof(PciCommonCfg, queueReset) == 58, "§4.1.4.3");

// §4.1.4.5 ISR status (8비트, 읽으면 0으로 지워진다)
namespace isr {
inline constexpr uint8_t kQueueInterrupt = 1 << 0;
inline constexpr uint8_t kConfigInterrupt = 1 << 1;
} // namespace isr

// §4.1.5.1.2
inline constexpr uint16_t kMsiNoVector = 0xFFFF;

// capability가 BAR 범위 안에 있는지 확인한다 (NFR-006). 덧셈 넘침도 거절한다.
constexpr bool isCapWithinBar(uint32_t capOffset, uint32_t capLength, uint64_t barSize)
{
    const uint64_t end = uint64_t{capOffset} + uint64_t{capLength};
    return capLength != 0 && end <= barSize;
}

} // namespace pci

} // namespace vv::virtio
