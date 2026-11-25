#include "TestMain.hpp"

#include <string.h>

#include "vv/Edid.hpp"

namespace e = vv::edid;

namespace {
// 헤더와 체크섬만 맞춘 최소 EDID 블록을 만든다.
void makeBlock(uint8_t *block, bool withHeader)
{
    memset(block, 0x11, e::kBlockBytes);
    if (withHeader) {
        memcpy(block, e::kHeader, sizeof(e::kHeader));
    }
    uint8_t sum = 0;
    for (size_t i = 0; i + 1 < e::kBlockBytes; ++i) {
        sum = static_cast<uint8_t>(sum + block[i]);
    }
    block[e::kBlockBytes - 1] = static_cast<uint8_t>(0x100 - sum);
}
} // namespace

VV_TEST(edid_validOneAndTwoBlocks)
{
    uint8_t data[256];
    makeBlock(data, true);
    VV_EXPECT(e::validate(data, 128, sizeof(data)) == e::Check::Ok);
    makeBlock(data + 128, false);  // 확장 블록은 헤더가 없다
    VV_EXPECT(e::validate(data, 256, sizeof(data)) == e::Check::Ok);
}

VV_TEST(edid_rejectsBadInputs)
{
    uint8_t data[256];
    makeBlock(data, true);
    VV_EXPECT(e::validate(data, 0, sizeof(data)) == e::Check::Empty);
    VV_EXPECT(e::validate(nullptr, 128, sizeof(data)) == e::Check::Empty);
    VV_EXPECT(e::validate(data, 512, sizeof(data)) == e::Check::TooLarge);
    VV_EXPECT(e::validate(data, 100, sizeof(data)) == e::Check::NotBlockMultiple);
    data[10] ^= 0x01;  // 체크섬 깨짐
    VV_EXPECT(e::validate(data, 128, sizeof(data)) == e::Check::BadChecksum);
    makeBlock(data, false);  // 헤더 없음
    VV_EXPECT(e::validate(data, 128, sizeof(data)) == e::Check::BadHeader);
    VV_EXPECT(e::checkName(e::Check::BadHeader)[0] != '\0');
}
