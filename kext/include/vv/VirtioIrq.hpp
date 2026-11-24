// 인터럽트 방식 선택과 벡터 배정 (WI-1.005, REQ-014, REQ-015).
// 커널 비의존 순수 로직이라 사용자 공간 단위 테스트로 검증한다.
#pragma once

#include <stdint.h>

#include "vv/VirtioGpuSpec.hpp"
#include "vv/VirtioSpec.hpp"

namespace vv::virtio {

enum class IrqMode : int {
    MsiX = 0,  // 기본. 벡터 3개 (config, controlq, cursorq)
    Intx = 1,  // 레거시 레벨 인터럽트 + ISR 레지스터
    Poll = 2,  // 디버그용. 타이머로 used 링과 config 이벤트를 확인한다
};

// boot-arg `vvideo_irq=<0|1|2>`. 범위 밖이면 기본값(MSI-X).
inline constexpr const char *kIrqModeBootArg = "vvideo_irq";
inline constexpr IrqMode kDefaultIrqMode = IrqMode::MsiX;

constexpr IrqMode irqModeFromBootArg(bool hasValue, int value)
{
    if (!hasValue || value < static_cast<int>(IrqMode::MsiX) || value > static_cast<int>(IrqMode::Poll)) {
        return kDefaultIrqMode;
    }
    return static_cast<IrqMode>(value);
}

constexpr const char *irqModeName(IrqMode mode)
{
    switch (mode) {
    case IrqMode::MsiX: return "msix";
    case IrqMode::Intx: return "intx";
    case IrqMode::Poll: return "poll";
    }
    return "unknown";
}

// MSI-X 테이블 항목 배정.
// 벡터를 따로 쓸 수 있으면 config 0, controlq 1, cursorq 2 (장치 `vectors=3`과 같다).
// macOS가 이 장치에 MSI-X 소스를 1개만 붙여 준 경우(WI-1.005 실측)에는 셋 다 벡터 0을 같이 쓴다.
// 여러 원인이 한 벡터를 공유하는 것은 사양상 허용된다 (§4.1.5.1.2).
inline constexpr uint16_t kConfigVector = 0;
inline constexpr uint16_t kControlQueueVector = 1;
inline constexpr uint16_t kCursorQueueVector = 2;
inline constexpr uint16_t kSeparateMsixVectors = 3;

constexpr bool usesSeparateVectors(uint16_t msixSources)
{
    return msixSources >= kSeparateMsixVectors;
}

constexpr uint16_t vectorForQueue(uint16_t queueIndex, uint16_t msixSources)
{
    if (!usesSeparateVectors(msixSources)) {
        return kConfigVector;
    }
    switch (queueIndex) {
    case gpu::kControlQueue: return kControlQueueVector;
    case gpu::kCursorQueue: return kCursorQueueVector;
    default: return pci::kMsiNoVector;
    }
}

// 방식별로 장치에 쓸 벡터. MSI-X가 아니면 NO_VECTOR로 두어 장치가 INTx/ISR을 쓰게 한다 (§4.1.5.3).
constexpr uint16_t configVectorFor(IrqMode mode)
{
    return mode == IrqMode::MsiX ? kConfigVector : pci::kMsiNoVector;
}

constexpr uint16_t queueVectorFor(IrqMode mode, uint16_t queueIndex, uint16_t msixSources)
{
    return mode == IrqMode::MsiX ? vectorForQueue(queueIndex, msixSources) : pci::kMsiNoVector;
}

// 실제로 쓸 방식. 요청한 방식을 지원하는 소스가 없으면 다음 방식으로 내려간다 (MSI-X → INTx → Poll).
constexpr IrqMode effectiveIrqMode(IrqMode requested, uint16_t msixSources, bool hasLegacySource)
{
    if (requested == IrqMode::MsiX && msixSources > 0) {
        return IrqMode::MsiX;
    }
    if (requested != IrqMode::Poll && hasLegacySource) {
        return IrqMode::Intx;
    }
    return IrqMode::Poll;
}

// §4.1.4.5 ISR 값 해석 (INTx일 때만 의미가 있다)
constexpr bool isrHasQueueEvent(uint8_t isr)
{
    return (isr & pci::isr::kQueueInterrupt) != 0;
}

constexpr bool isrHasConfigEvent(uint8_t isr)
{
    return (isr & pci::isr::kConfigInterrupt) != 0;
}

} // namespace vv::virtio
