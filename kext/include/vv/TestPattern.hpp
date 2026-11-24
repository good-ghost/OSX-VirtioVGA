// 색상 확인용 시험 패턴 (WI-2.004, AC-04, REQ-022).
// 화면을 세로로 4등분해 빨강·초록·파랑·흰색 띠를 그린다. 픽셀은 macOS 32bpp와 같은 메모리 순서 B,G,R,X
// (VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM)다. 색이 뒤바뀌면 형식 선택이 틀린 것이다.
// 커널 비의존 순수 로직이라 사용자 공간 단위 테스트로 검증한다.
#pragma once

#include <stdint.h>

namespace vv::display {

// 메모리 바이트가 B,G,R,X 순서가 되는 리틀 엔디언 32비트 값 (0xXXRRGGBB).
inline constexpr uint32_t kPatternRed = 0x00FF0000;
inline constexpr uint32_t kPatternGreen = 0x0000FF00;
inline constexpr uint32_t kPatternBlue = 0x000000FF;
inline constexpr uint32_t kPatternWhite = 0x00FFFFFF;
inline constexpr uint32_t kPatternBands[] = {kPatternRed, kPatternGreen, kPatternBlue, kPatternWhite};
inline constexpr uint32_t kPatternBandCount = sizeof(kPatternBands) / sizeof(kPatternBands[0]);

constexpr uint32_t patternColorAt(uint32_t x, uint32_t width)
{
    // x < width이면 결과는 kPatternBandCount 미만이라 32비트로 줄여도 안전하다.
    const uint32_t band = width == 0 ? 0 : static_cast<uint32_t>((uint64_t{x} * kPatternBandCount) / width);
    return kPatternBands[band < kPatternBandCount ? band : kPatternBandCount - 1];
}

// pixels: rowPixels × height 크기의 32비트 픽셀 배열.
inline void fillPattern(uint32_t *pixels, uint32_t width, uint32_t height, uint32_t rowPixels)
{
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t *row = pixels + uint64_t{y} * rowPixels;
        for (uint32_t x = 0; x < width; ++x) {
            row[x] = patternColorAt(x, width);
        }
    }
}

} // namespace vv::display
