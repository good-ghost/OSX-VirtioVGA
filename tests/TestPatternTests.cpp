#include "TestMain.hpp"

#include <cstring>
#include <vector>

#include "vv/TestPattern.hpp"

namespace d = vv::display;

VV_TEST(pattern_bandsInOrder)
{
    VV_EXPECT(d::patternColorAt(0, 1280) == d::kPatternRed);
    VV_EXPECT(d::patternColorAt(319, 1280) == d::kPatternRed);
    VV_EXPECT(d::patternColorAt(320, 1280) == d::kPatternGreen);
    VV_EXPECT(d::patternColorAt(640, 1280) == d::kPatternBlue);
    VV_EXPECT(d::patternColorAt(1279, 1280) == d::kPatternWhite);
    VV_EXPECT(d::patternColorAt(5, 0) == d::kPatternRed);  // 폭 0 방어
}

VV_TEST(pattern_memoryByteOrderIsBGRX)
{
    // B8G8R8X8_UNORM: 메모리 바이트가 B, G, R, X 순서여야 한다.
    uint8_t bytes[4];
    std::memcpy(bytes, &d::kPatternRed, 4);
    VV_EXPECT(bytes[0] == 0x00 && bytes[1] == 0x00 && bytes[2] == 0xFF);
    std::memcpy(bytes, &d::kPatternBlue, 4);
    VV_EXPECT(bytes[0] == 0xFF && bytes[1] == 0x00 && bytes[2] == 0x00);
}

VV_TEST(pattern_fillRespectsRowPitch)
{
    constexpr uint32_t w = 8, h = 2, row = 10;
    std::vector<uint32_t> pixels(row * h, 0xDEADBEEF);
    d::fillPattern(pixels.data(), w, h, row);
    VV_EXPECT(pixels[0] == d::kPatternRed && pixels[7] == d::kPatternWhite);
    VV_EXPECT(pixels[8] == 0xDEADBEEF && pixels[9] == 0xDEADBEEF);  // 줄 끝 여백은 건드리지 않는다
    VV_EXPECT(pixels[row + 2] == d::kPatternGreen);
}
