// 변경 감지 전송 (WI-4.004, NFR-001). 커널에 의존하지 않아 단위 테스트한다.
//
// 화면을 tile×tile 타일로 나누고, 직전에 본 내용(shadow)과 비교해 바뀐 타일만 찾는다. 바뀐 행은 shadow에 복사해
// 다음 비교의 기준으로 삼는다. 결과는 타일 행(band)마다 가장 왼쪽~오른쪽 바뀐 타일을 잇는 사각형 하나로 만들고,
// 이웃한 band의 가로 범위가 같으면 세로로 합친다. 명령 수(왕복)를 줄이기 위해서다.
//
// 동시 쓰기: WindowServer가 비교 중에 프레임버퍼를 바꿔도 놓치지 않는다. shadow에는 비교한 시점의 값이 들어가므로
// 그 뒤의 변경은 다음 주기 비교에서 다시 드러난다. 전송은 장치가 명령 시점의 프레임버퍼를 읽는다.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace vv::dirty {

inline constexpr uint32_t kBytesPerPixel = 4;
inline constexpr uint32_t kMinTile = 16;
inline constexpr uint32_t kMaxTile = 256;
inline constexpr uint32_t kDefaultTile = 64;

struct Rect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

constexpr bool operator==(const Rect &a, const Rect &b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

// 설정값(타일 크기)을 검사한다. 16~256의 2의 거듭제곱만 받고 나머지는 기본값.
constexpr uint32_t normalizeTile(uint32_t tile)
{
    return (tile >= kMinTile && tile <= kMaxTile && (tile & (tile - 1)) == 0) ? tile : kDefaultTile;
}

// 사각형들을 모두 덮는 사각형. 없으면 0 크기.
inline Rect bounds(const Rect *rects, size_t count)
{
    if (rects == nullptr || count == 0) {
        return Rect{};
    }
    uint32_t left = rects[0].x;
    uint32_t top = rects[0].y;
    uint32_t right = rects[0].x + rects[0].width;
    uint32_t bottom = rects[0].y + rects[0].height;
    for (size_t i = 1; i < count; ++i) {
        left = rects[i].x < left ? rects[i].x : left;
        top = rects[i].y < top ? rects[i].y : top;
        right = rects[i].x + rects[i].width > right ? rects[i].x + rects[i].width : right;
        bottom = rects[i].y + rects[i].height > bottom ? rects[i].y + rects[i].height : bottom;
    }
    return Rect{left, top, right - left, bottom - top};
}

// current와 shadow를 비교해 바뀐 영역을 out에 채우고 개수를 돌려준다. 바뀐 행은 shadow에 복사한다.
// pitch는 두 버퍼 모두의 행 간격(바이트). out이 넘치면 전체를 덮는 사각형 하나로 바꿔 1을 돌려준다.
// 바뀐 것이 없으면 0.
inline size_t scan(const uint8_t *current, uint8_t *shadow, uint32_t width, uint32_t height, uint32_t pitch,
                   uint32_t tile, Rect *out, size_t maxRects)
{
    if (current == nullptr || shadow == nullptr || out == nullptr || maxRects == 0 || width == 0 || height == 0 ||
        pitch < width * kBytesPerPixel) {
        return 0;
    }
    tile = normalizeTile(tile);
    const uint32_t columns = (width + tile - 1) / tile;
    size_t count = 0;
    bool overflow = false;
    for (uint32_t bandTop = 0; bandTop < height; bandTop += tile) {
        const uint32_t bandHeight = height - bandTop < tile ? height - bandTop : tile;
        uint32_t firstColumn = columns;  // 바뀐 타일이 없으면 columns
        uint32_t lastColumn = 0;
        for (uint32_t row = bandTop; row < bandTop + bandHeight; ++row) {
            const uint8_t *src = current + static_cast<size_t>(row) * pitch;
            uint8_t *dst = shadow + static_cast<size_t>(row) * pitch;
            for (uint32_t column = 0; column < columns; ++column) {
                const uint32_t x = column * tile;
                const uint32_t span = (width - x < tile ? width - x : tile) * kBytesPerPixel;
                const size_t offset = static_cast<size_t>(x) * kBytesPerPixel;
                if (__builtin_memcmp(src + offset, dst + offset, span) != 0) {
                    __builtin_memcpy(dst + offset, src + offset, span);
                    firstColumn = column < firstColumn ? column : firstColumn;
                    lastColumn = column > lastColumn ? column : lastColumn;
                }
            }
        }
        if (firstColumn == columns) {
            continue;
        }
        const uint32_t x = firstColumn * tile;
        const uint32_t right = (lastColumn + 1) * tile < width ? (lastColumn + 1) * tile : width;
        const Rect band{x, bandTop, right - x, bandHeight};
        // 바로 위 사각형과 가로 범위가 같고 맞닿아 있으면 세로로 늘린다.
        if (count > 0 && out[count - 1].x == band.x && out[count - 1].width == band.width &&
            out[count - 1].y + out[count - 1].height == band.y) {
            out[count - 1].height += band.height;
            continue;
        }
        if (count == maxRects) {
            overflow = true;  // 비교·복사는 끝까지 해서 shadow를 맞춘다
            continue;
        }
        out[count++] = band;
    }
    if (overflow) {
        // 넘친 사각형까지 덮어야 하므로 화면 전체 폭·높이의 범위를 다시 구하지 않고 전체로 보낸다.
        out[0] = Rect{0, 0, width, height};
        return 1;
    }
    return count;
}

// 사각형의 왼쪽 위가 backing에서 시작하는 바이트 위치 (TRANSFER_TO_HOST_2D의 offset, §5.7.6.5).
constexpr uint64_t backingOffset(const Rect &rect, uint32_t pitch)
{
    return static_cast<uint64_t>(rect.y) * pitch + static_cast<uint64_t>(rect.x) * kBytesPerPixel;
}

} // namespace vv::dirty
