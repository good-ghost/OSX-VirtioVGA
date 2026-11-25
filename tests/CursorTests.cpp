#include "TestMain.hpp"

#include "vv/Cursor.hpp"

namespace c = vv::cursor;
namespace g = vv::virtio::gpu;

VV_TEST(cursor_pointerPosition_addsHotspotAndClamps)
{
    const auto inside = c::pointerPosition(100, 200, 4, 5);
    VV_EXPECT(inside.x == 104 && inside.y == 205);
    // 화면 왼쪽·위로 일부 나간 커서: 왼쪽 위는 음수지만 포인터는 화면 안이다.
    const auto partly = c::pointerPosition(-3, -2, 4, 5);
    VV_EXPECT(partly.x == 1 && partly.y == 3);
    // 포인터까지 화면 밖이면 0으로 자른다.
    const auto outside = c::pointerPosition(-50, -60, 4, 5);
    VV_EXPECT(outside.x == 0 && outside.y == 0);
}

VV_TEST(cursor_fits_limitsSizeAndHotspot)
{
    VV_EXPECT(c::fits(32, 32, 0, 0));
    VV_EXPECT(c::fits(64, 64, 63, 63));
    VV_EXPECT(!c::fits(65, 64, 0, 0));
    VV_EXPECT(!c::fits(64, 0, 0, 0));
    VV_EXPECT(!c::fits(64, 64, 64, 0));
}

VV_TEST(cursor_makeCommand_fillsSpecFields)
{
    const auto update = c::makeCommand(g::CtrlType::CmdUpdateCursor, 0, 7, c::Position{10, 20}, 3, 4);
    VV_EXPECT(update.hdr.type == 0x0300);
    VV_EXPECT(update.pos.scanoutId == 0 && update.pos.x == 10 && update.pos.y == 20);
    VV_EXPECT(update.resourceId == 7 && update.hotX == 3 && update.hotY == 4);
    VV_EXPECT(update.hdr.flags == 0 && update.hdr.fenceId == 0 && update.padding == 0);
    const auto move = c::makeCommand(g::CtrlType::CmdMoveCursor, 0, 0, c::Position{1, 2}, 0, 0);
    VV_EXPECT(move.hdr.type == 0x0301);
    VV_EXPECT(c::kBytes == 64u * 64u * 4u);
    VV_EXPECT(c::kFormat == g::Format::B8G8R8A8Unorm);
}

VV_TEST(cursor_repackRows_movesPackedRowsToPitch)
{
    // 폭 3, 높이 4인 이미지를 행 간격 3픽셀로 붙여 쓴 상태를 만든다. 픽셀 값 = 행*16 + 열 + 1.
    static uint32_t pixels[c::kSize * c::kSize];
    for (auto &p : pixels) {
        p = 0;
    }
    const uint32_t width = 3;
    const uint32_t height = 4;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            pixels[y * width + x] = y * 16 + x + 1;
        }
    }
    c::repackRows(reinterpret_cast<uint8_t *>(pixels), width, height);
    bool ok = true;
    for (uint32_t y = 0; y < c::kSize; ++y) {
        for (uint32_t x = 0; x < c::kSize; ++x) {
            const uint32_t expected = (y < height && x < width) ? y * 16 + x + 1 : 0;
            ok = ok && pixels[y * c::kSize + x] == expected;
        }
    }
    VV_EXPECT(ok);
}

VV_TEST(cursor_repackRows_fullWidthUnchanged)
{
    static uint32_t pixels[c::kSize * c::kSize];
    for (uint32_t i = 0; i < c::kSize * c::kSize; ++i) {
        pixels[i] = i;
    }
    c::repackRows(reinterpret_cast<uint8_t *>(pixels), c::kSize, c::kSize);
    bool ok = true;
    for (uint32_t i = 0; i < c::kSize * c::kSize; ++i) {
        ok = ok && pixels[i] == i;
    }
    VV_EXPECT(ok);
}

VV_TEST(cursor_halve_averagesAndHalvesHotspot)
{
    static uint8_t pixels[c::kSize * c::kSize * c::kBytesPerPixel];
    for (auto &b : pixels) {
        b = 0;
    }
    const uint32_t pitch = c::kSize * c::kBytesPerPixel;
    // 3×2 이미지: (0,0)=255 불투명 흰색, (1,0)=0, (0,1)=255, (1,1)=0, (2,0)=(2,1)=100.
    auto put = [&](uint32_t x, uint32_t y, uint8_t v) {
        for (uint32_t ch = 0; ch < c::kBytesPerPixel; ++ch) {
            pixels[y * pitch + x * c::kBytesPerPixel + ch] = v;
        }
    };
    put(0, 0, 255);
    put(0, 1, 255);
    put(2, 0, 100);
    put(2, 1, 100);
    put(5, 5, 77);  // 이미지 밖의 찌꺼기는 지워져야 한다
    const auto out = c::halve(pixels, c::Image{3, 2, 5, 3});
    VV_EXPECT(out.width == 2 && out.height == 1 && out.hotX == 2 && out.hotY == 1);
    VV_EXPECT(pixels[0] == 128);                       // (255+0+255+0)/4 반올림
    VV_EXPECT(pixels[c::kBytesPerPixel] == 100);       // 오른쪽 열은 두 픽셀만 평균
    VV_EXPECT(pixels[pitch] == 0);                     // 줄인 영역 아래는 투명
    VV_EXPECT(pixels[5 * pitch + 5 * c::kBytesPerPixel] == 0);
}

VV_TEST(cursor_normalizeScale_onlyHalfOrFull)
{
    VV_EXPECT(c::normalizeScale(50) == 50);
    VV_EXPECT(c::normalizeScale(100) == 100);
    VV_EXPECT(c::normalizeScale(0) == 100);
    VV_EXPECT(c::normalizeScale(75) == 100);
}
