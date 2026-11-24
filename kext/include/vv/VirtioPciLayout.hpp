// virtio PCI capability 목록에서 쓸 영역(common/notify/isr/device)을 고르고 검증한다 (WI-1.002, REQ-010).
// 커널 비의존 순수 로직이라 사용자 공간 단위 테스트로 검증한다.
// 사양 근거: VIRTIO 1.2 §4.1.4 — 같은 종류의 구조체가 여러 개면 드라이버는 지원 가능한 첫 번째 것을 쓴다(SHOULD).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "vv/PciCapWalk.hpp"
#include "vv/VirtioSpec.hpp"

namespace vv::virtio::pci {

inline constexpr unsigned kBarCount = kMaxBarIndex + 1;

// VIRTIO 1.0 필드(queue_device까지)를 읽을 수 있어야 한다. queue_notify_data/queue_reset은 선택 기능이다.
inline constexpr uint32_t kMinCommonCfgBytes = offsetof(PciCommonCfg, queueNotifyData);
static_assert(kMinCommonCfgBytes == 56, "§4.1.4.3 VIRTIO 1.0 필드 끝");
inline constexpr uint32_t kMinIsrBytes = 1;
// 알림 쓰기는 16비트(queue index)다. 큐 수를 알기 전에는 최소 한 번의 쓰기 공간만 확인한다.
inline constexpr uint32_t kMinNotifyBytes = sizeof(uint16_t);

struct Region {
    bool present = false;
    uint8_t bar = 0;
    uint32_t offset = 0;
    uint32_t length = 0;
};

struct Layout {
    Region common;
    Region notify;
    Region isr;
    Region device;
    uint32_t notifyOffMultiplier = 0;
};

enum class LayoutError {
    None,
    MissingCommon,
    MissingNotify,
    MissingIsr,
    MissingDevice,
    BadNotifyMultiplier,
};

constexpr const char *layoutErrorName(LayoutError error)
{
    switch (error) {
    case LayoutError::None: return "none";
    case LayoutError::MissingCommon: return "missing-common";
    case LayoutError::MissingNotify: return "missing-notify";
    case LayoutError::MissingIsr: return "missing-isr";
    case LayoutError::MissingDevice: return "missing-device";
    case LayoutError::BadNotifyMultiplier: return "bad-notify-multiplier";
    }
    return "unknown";
}

// §4.1.4.4: notify_off_multiplier는 0이거나 짝수인 2의 거듭제곱이어야 한다.
constexpr bool isValidNotifyMultiplier(uint32_t multiplier)
{
    if (multiplier == 0) {
        return true;
    }
    const bool powerOfTwo = (multiplier & (multiplier - 1)) == 0;
    return powerOfTwo && multiplier >= 2;
}

constexpr uint32_t minBytesFor(uint8_t cfgType, uint32_t minDeviceCfgBytes)
{
    switch (cfgType) {
    case capType::kCommonCfg: return kMinCommonCfgBytes;
    case capType::kNotifyCfg: return kMinNotifyBytes;
    case capType::kIsrCfg: return kMinIsrBytes;
    case capType::kDeviceCfg: return minDeviceCfgBytes;
    default: return 0;
    }
}

// caps에서 종류마다 "BAR 범위 안에 있고 최소 길이를 만족하는" 첫 capability를 고른다.
// barSizes[i] == 0이면 그 BAR는 없거나 쓸 수 없는 것으로 본다.
// minDeviceCfgBytes: 장치 종류별 config 구조체 크기 (virtio-gpu는 sizeof(gpu::Config)).
inline LayoutError buildLayout(const vv::pci::VirtioCap *caps, size_t count, const uint64_t (&barSizes)[kBarCount],
                               uint32_t minDeviceCfgBytes, Layout &out)
{
    out = Layout{};
    for (size_t i = 0; i < count; ++i) {
        const vv::pci::VirtioCap &cap = caps[i];
        Region *slot = nullptr;
        switch (cap.cfgType) {
        case capType::kCommonCfg: slot = &out.common; break;
        case capType::kNotifyCfg: slot = &out.notify; break;
        case capType::kIsrCfg: slot = &out.isr; break;
        case capType::kDeviceCfg: slot = &out.device; break;
        default: break;  // pci cfg 창, 공유 메모리, vendor cfg는 쓰지 않는다
        }
        if (slot == nullptr || slot->present || cap.bar >= kBarCount) {
            continue;
        }
        if (!isCapWithinBar(cap.offset, cap.length, barSizes[cap.bar]) ||
            cap.length < minBytesFor(cap.cfgType, minDeviceCfgBytes)) {
            continue;  // 쓸 수 없는 것은 건너뛰고 같은 종류의 다음 것을 본다
        }
        slot->present = true;
        slot->bar = cap.bar;
        slot->offset = cap.offset;
        slot->length = cap.length;
        if (cap.cfgType == capType::kNotifyCfg) {
            out.notifyOffMultiplier = cap.notifyOffMultiplier;
        }
    }
    if (!out.common.present) {
        return LayoutError::MissingCommon;
    }
    if (!out.notify.present) {
        return LayoutError::MissingNotify;
    }
    if (!out.isr.present) {
        return LayoutError::MissingIsr;
    }
    if (!out.device.present) {
        return LayoutError::MissingDevice;
    }
    if (!isValidNotifyMultiplier(out.notifyOffMultiplier)) {
        return LayoutError::BadNotifyMultiplier;
    }
    return LayoutError::None;
}

// 큐 알림 주소의 notify 영역 안 오프셋 (§4.1.4.4). 영역을 벗어나면 false.
constexpr bool notifyOffsetFor(const Layout &layout, uint16_t queueNotifyOff, uint32_t &offsetOut)
{
    const uint64_t offset = uint64_t{queueNotifyOff} * layout.notifyOffMultiplier;
    if (offset + kMinNotifyBytes > layout.notify.length) {
        return false;
    }
    offsetOut = static_cast<uint32_t>(offset);
    return true;
}

} // namespace vv::virtio::pci
