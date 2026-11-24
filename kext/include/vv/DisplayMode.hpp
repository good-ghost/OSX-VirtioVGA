// 해상도·프레임버퍼 크기 계산 (WI-2.002, WI-2.003, REQ-036, REQ-040, Q-02, Q-03).
// 커널 비의존 순수 로직이라 사용자 공간 단위 테스트로 검증한다.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "vv/VirtioGpuSpec.hpp"

namespace vv::display {

struct Mode {
    uint32_t width = 0;
    uint32_t height = 0;
};

constexpr bool operator==(const Mode &a, const Mode &b)
{
    return a.width == b.width && a.height == b.height;
}

// Q-02 확정: 최대 2560×1600. 이보다 큰 선호 크기는 쓰지 않는다.
inline constexpr Mode kMaxMode = {2560, 1600};
// 장치가 선호 크기를 주지 않거나 쓸 수 없을 때. QEMU virtio-vga 기본 xres/yres와 같다 (REQUIREMENT §8.1.1).
inline constexpr Mode kFallbackMode = {1280, 800};
// macOS 32bpp (REQ-022). 한 줄 바이트 수는 width × 4로 두고 따로 정렬하지 않는다.
inline constexpr uint32_t kBytesPerPixel = 4;
// Q-03 확정: 프레임버퍼 메모리 풀 128 MiB. 이 크기를 VRAM으로 보고한다 (REQ-060).
inline constexpr uint64_t kFramebufferPoolBytes = 128ull * 1024 * 1024;
inline constexpr uint64_t kPageBytes = 4096;

constexpr bool isUsable(const Mode &mode)
{
    return mode.width != 0 && mode.height != 0 && mode.width <= kMaxMode.width && mode.height <= kMaxMode.height;
}

constexpr uint32_t rowBytes(const Mode &mode)
{
    return mode.width * kBytesPerPixel;
}

// 프레임버퍼 하나의 바이트 수 (page 단위로 올림). 64비트로 계산해 넘침을 막는다.
constexpr uint64_t framebufferBytes(const Mode &mode)
{
    const uint64_t raw = uint64_t{mode.width} * mode.height * kBytesPerPixel;
    return (raw + kPageBytes - 1) & ~(kPageBytes - 1);
}

constexpr bool fitsInPool(const Mode &mode, uint64_t alreadyUsed = 0)
{
    return framebufferBytes(mode) + alreadyUsed <= kFramebufferPoolBytes;
}

struct PreferredMode {
    Mode mode;
    bool fallbackUsed = false;  // 장치 값을 쓰지 않고 kFallbackMode를 썼다
};

// display info의 scanout 하나에서 선호 해상도를 고른다 (WI-2.002). 꺼져 있거나 0이거나 상한을 넘으면 대체값.
constexpr PreferredMode preferredModeFrom(const vv::virtio::gpu::DisplayOne &scanout)
{
    const Mode reported{scanout.r.width, scanout.r.height};
    if (scanout.enabled == 0 || !isUsable(reported)) {
        return {kFallbackMode, true};
    }
    return {reported, false};
}

} // namespace vv::display
