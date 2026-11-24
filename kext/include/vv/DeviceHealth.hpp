// 장치 상태 판정과 복구 정책 (WI-1.006, REQ-017).
// 커널 비의존 순수 로직이라 사용자 공간 단위 테스트로 검증한다.
#pragma once

#include <stdint.h>

#include "vv/VirtioSpec.hpp"

namespace vv::virtio {

enum class DeviceHealth {
    Ok,          // DRIVER_OK가 켜져 있고 이상 비트가 없다
    NeedsReset,  // 장치가 DEVICE_NEEDS_RESET을 올렸다 (§2.1.2)
    Lost,        // DRIVER_OK가 사라졌다 — 외부에서 reset됐거나 장치가 초기 상태로 돌아갔다
};

constexpr DeviceHealth classifyStatus(uint8_t status)
{
    if ((status & status::kDeviceNeedsReset) != 0) {
        return DeviceHealth::NeedsReset;
    }
    if ((status & status::kDriverOk) == 0) {
        return DeviceHealth::Lost;
    }
    return DeviceHealth::Ok;
}

constexpr const char *deviceHealthName(DeviceHealth health)
{
    switch (health) {
    case DeviceHealth::Ok: return "ok";
    case DeviceHealth::NeedsReset: return "needs-reset";
    case DeviceHealth::Lost: return "lost";
    }
    return "unknown";
}

// 복구를 무한히 되풀이하지 않도록 한 번 시작(start)마다 시도 횟수를 제한한다.
// 이 횟수를 넘으면 장치를 FAILED로 두고 손을 뗀다 (화면은 IONDRV가 계속 그린다).
inline constexpr uint32_t kMaxRecoveries = 3;

constexpr bool mayRecover(uint32_t recoveriesSoFar)
{
    return recoveriesSoFar < kMaxRecoveries;
}

} // namespace vv::virtio
