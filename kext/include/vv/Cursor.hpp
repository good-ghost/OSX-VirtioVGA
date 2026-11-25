// 하드웨어 커서 계산 (WI-4.001, REQ-033). 커널에 의존하지 않아 단위 테스트한다.
//
// IOFramebuffer::setCursorState는 커서 이미지의 왼쪽 위 좌표를 준다(핫스팟을 뺀 값, 화면 밖이면 음수).
// virtio-gpu cursorq의 pos는 포인터 위치(핫스팟이 놓일 곳)이고 장치가 hot_x/hot_y만큼 당겨 그린다
// (§5.7.6.10 struct virtio_gpu_update_cursor). 그래서 pos = 왼쪽 위 + 핫스팟으로 바꾼다.
#pragma once

#include <stdint.h>

#include "vv/VirtioGpuSpec.hpp"

namespace vv::cursor {

// 장치에 넘기는 커서 resource 크기. QEMU virtio-gpu는 64×64 커서를 쓴다.
inline constexpr uint32_t kSize = 64;
inline constexpr uint32_t kBytesPerPixel = 4;
inline constexpr uint32_t kBytes = kSize * kSize * kBytesPerPixel;
// IOGraphics가 kIO32ARGBPixelFormat으로 변환하면 32비트 값 0xAARRGGBB를 little-endian으로 쓴다.
// 메모리 바이트 순서는 B,G,R,A이므로 B8G8R8A8_UNORM이다.
inline constexpr virtio::gpu::Format kFormat = virtio::gpu::Format::B8G8R8A8Unorm;

struct Position {
    uint32_t x = 0;
    uint32_t y = 0;
};

// 왼쪽 위 좌표와 핫스팟에서 포인터 위치를 구한다. 결과가 음수면 0으로 자른다(cursorq 좌표는 부호 없음).
constexpr Position pointerPosition(int32_t left, int32_t top, uint32_t hotX, uint32_t hotY)
{
    const int64_t x = static_cast<int64_t>(left) + hotX;
    const int64_t y = static_cast<int64_t>(top) + hotY;
    return Position{x < 0 ? 0u : static_cast<uint32_t>(x), y < 0 ? 0u : static_cast<uint32_t>(y)};
}

// 변환된 커서가 resource에 들어가는지. 핫스팟도 이미지 안이어야 한다.
constexpr bool fits(uint32_t width, uint32_t height, uint32_t hotX, uint32_t hotY)
{
    return width > 0 && height > 0 && width <= kSize && height <= kSize && hotX < kSize && hotY < kSize;
}

// convertCursorImage가 쓴 이미지를 resource 행 간격(kSize 픽셀)으로 다시 놓는다.
// IOGraphics는 행을 커서 폭(width)으로 붙여 쓴다. 0.0.38 실측: 폭 64인 화살표는 정상이고, 좁은 I 커서는
// 짝수·홀수 행이 좌우로 놓여 두 개로 보였다. 버퍼는 변환 전에 0으로 비워 둔 상태여야 한다.
// 뒤 행부터 옮기므로 앞 행의 원본을 덮지 않는다 (행 y의 목적지 y*pitch ≥ 원본 y*packed).
inline void repackRows(uint8_t *pixels, uint32_t width, uint32_t height)
{
    if (pixels == nullptr || width == 0 || width >= kSize || height == 0 || height > kSize) {
        return;
    }
    const uint32_t packed = width * kBytesPerPixel;
    const uint32_t pitch = kSize * kBytesPerPixel;
    for (uint32_t y = height - 1; y > 0; --y) {
        __builtin_memmove(pixels + y * pitch, pixels + y * packed, packed);
    }
    // 옮기고 남은 원본 조각을 지운다. 각 행의 커서 폭 뒤는 투명이어야 한다.
    for (uint32_t y = 0; y < height; ++y) {
        __builtin_memset(pixels + y * pitch + packed, 0, pitch - packed);
    }
}

// 커서 크기 비율(퍼센트). 호스트 창이 게스트 화면을 줄여 보여 줄 때 쓴다: QEMU GTK는 커서를 창 배율 없이
// 픽셀 그대로 그리므로, HiDPI 2560×1600 게스트를 1280×800 창에 보이면 커서만 2배로 보인다 (0.0.38 실측).
inline constexpr uint32_t kScaleFull = 100;
inline constexpr uint32_t kScaleHalf = 50;

// boot-arg 값을 지원하는 비율로 바꾼다. 50만 줄이고 나머지는 원래 크기.
constexpr uint32_t normalizeScale(uint32_t percent)
{
    return percent == kScaleHalf ? kScaleHalf : kScaleFull;
}

struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t hotX = 0;
    uint32_t hotY = 0;
};

// resource 행 간격(kSize)으로 놓인 이미지를 가로·세로 절반으로 줄여 왼쪽 위에 다시 쓴다. 2×2 픽셀의 채널별 평균.
// 행 우선으로 쓰면 목적지(y,x)는 아직 읽지 않은 원본(2y.., 2x..)보다 앞이라 제자리에서 할 수 있다.
// 줄인 영역 밖은 투명(0)으로 지운다. 핫스팟도 절반으로 바꾼다.
inline Image halve(uint8_t *pixels, Image image)
{
    if (pixels == nullptr || image.width == 0 || image.height == 0 || image.width > kSize || image.height > kSize) {
        return image;
    }
    const uint32_t pitch = kSize * kBytesPerPixel;
    const Image out{(image.width + 1) / 2, (image.height + 1) / 2, image.hotX / 2, image.hotY / 2};
    for (uint32_t y = 0; y < out.height; ++y) {
        for (uint32_t x = 0; x < out.width; ++x) {
            uint32_t sum[kBytesPerPixel] = {};
            uint32_t count = 0;
            for (uint32_t dy = 0; dy < 2; ++dy) {
                for (uint32_t dx = 0; dx < 2; ++dx) {
                    const uint32_t sx = x * 2 + dx;
                    const uint32_t sy = y * 2 + dy;
                    if (sx >= image.width || sy >= image.height) {
                        continue;
                    }
                    const uint8_t *src = pixels + sy * pitch + sx * kBytesPerPixel;
                    for (uint32_t ch = 0; ch < kBytesPerPixel; ++ch) {
                        sum[ch] += src[ch];
                    }
                    ++count;
                }
            }
            uint8_t *dst = pixels + y * pitch + x * kBytesPerPixel;
            for (uint32_t ch = 0; ch < kBytesPerPixel; ++ch) {
                dst[ch] = static_cast<uint8_t>((sum[ch] + count / 2) / count);
            }
        }
    }
    for (uint32_t y = 0; y < kSize; ++y) {
        const uint32_t keep = y < out.height ? out.width * kBytesPerPixel : 0;
        __builtin_memset(pixels + y * pitch + keep, 0, pitch - keep);
    }
    return out;
}

// UPDATE_CURSOR(이미지·핫스팟 갱신, resourceId 0이면 숨김) 또는 MOVE_CURSOR(위치만) 요청을 만든다.
// MOVE_CURSOR에서 장치는 pos만 읽는다.
inline virtio::gpu::UpdateCursor makeCommand(virtio::gpu::CtrlType type, uint32_t scanoutId, uint32_t resourceId,
                                              Position pos, uint32_t hotX, uint32_t hotY)
{
    virtio::gpu::UpdateCursor request{};
    request.hdr.type = static_cast<uint32_t>(type);
    request.pos.scanoutId = scanoutId;
    request.pos.x = pos.x;
    request.pos.y = pos.y;
    request.resourceId = resourceId;
    request.hotX = hotX;
    request.hotY = hotY;
    return request;
}

} // namespace vv::cursor
