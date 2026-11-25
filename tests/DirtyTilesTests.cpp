#include "TestMain.hpp"

#include <string.h>

#include <vector>

#include "vv/DirtyTiles.hpp"

namespace d = vv::dirty;

namespace {
struct Frame {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    std::vector<uint8_t> current;
    std::vector<uint8_t> shadow;
    Frame(uint32_t w, uint32_t h)
        : width(w), height(h), pitch(w * d::kBytesPerPixel), current(pitch * h, 0), shadow(pitch * h, 0)
    {
    }
    void poke(uint32_t x, uint32_t y, uint8_t value) { current[y * pitch + x * d::kBytesPerPixel] = value; }
    size_t scan(uint32_t tile, d::Rect *out, size_t maxRects)
    {
        return d::scan(current.data(), shadow.data(), width, height, pitch, tile, out, maxRects);
    }
};
} // namespace

VV_TEST(dirty_scan_noChangeGivesNothing)
{
    Frame frame(200, 100);
    d::Rect rects[8];
    VV_EXPECT(frame.scan(64, rects, 8) == 0);
}

VV_TEST(dirty_scan_singlePixelMarksOneTileAndSyncsShadow)
{
    Frame frame(200, 100);
    frame.poke(70, 10, 9);
    d::Rect rects[8];
    VV_EXPECT(frame.scan(64, rects, 8) == 1);
    VV_EXPECT((rects[0] == d::Rect{64, 0, 64, 64}));
    // shadow가 맞춰졌으므로 다음 비교에는 변화가 없다.
    VV_EXPECT(frame.scan(64, rects, 8) == 0);
}

VV_TEST(dirty_scan_clipsEdgeTilesAndMergesBands)
{
    // 폭 200 = 타일 64×3 + 8. 오른쪽 끝 열과 아래 끝 행은 잘린다.
    Frame frame(200, 100);
    frame.poke(199, 5, 1);   // band 0, 마지막 열
    frame.poke(199, 99, 1);  // band 1(높이 36), 마지막 열
    d::Rect rects[8];
    VV_EXPECT(frame.scan(64, rects, 8) == 1);  // 가로 범위가 같은 두 band가 하나로 합쳐진다
    VV_EXPECT((rects[0] == d::Rect{192, 0, 8, 100}));
}

VV_TEST(dirty_scan_spanCoversLeftmostToRightmost)
{
    Frame frame(256, 64);
    frame.poke(0, 0, 1);
    frame.poke(200, 63, 1);
    d::Rect rects[8];
    VV_EXPECT(frame.scan(64, rects, 8) == 1);
    VV_EXPECT((rects[0] == d::Rect{0, 0, 256, 64}));
}

VV_TEST(dirty_scan_overflowFallsBackToFullFrame)
{
    // band마다 가로 범위를 다르게 해 합쳐지지 않게 한다.
    Frame frame(256, 256);
    for (uint32_t band = 0; band < 4; ++band) {
        frame.poke(band * 64, band * 64, 1);
    }
    d::Rect rects[2];
    VV_EXPECT(frame.scan(64, rects, 2) == 1);
    VV_EXPECT((rects[0] == d::Rect{0, 0, 256, 256}));
    // 넘쳐도 shadow는 모두 맞춰진다.
    VV_EXPECT(frame.scan(64, rects, 2) == 0);
}

VV_TEST(dirty_helpers_tileBoundsOffset)
{
    VV_EXPECT(d::normalizeTile(64) == 64);
    VV_EXPECT(d::normalizeTile(128) == 128);
    VV_EXPECT(d::normalizeTile(48) == d::kDefaultTile);
    VV_EXPECT(d::normalizeTile(8) == d::kDefaultTile);
    VV_EXPECT(d::normalizeTile(512) == d::kDefaultTile);
    const d::Rect rects[] = {{64, 0, 64, 64}, {0, 128, 32, 16}};
    VV_EXPECT((d::bounds(rects, 2) == d::Rect{0, 0, 128, 144}));
    VV_EXPECT((d::bounds(nullptr, 0) == d::Rect{}));
    VV_EXPECT(d::backingOffset(d::Rect{3, 2, 1, 1}, 100) == 212);
}
