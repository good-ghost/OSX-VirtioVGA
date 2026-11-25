#include "TestMain.hpp"

#include "vv/ModeTable.hpp"

namespace d = vv::display;

namespace {
const uint64_t kMaxBuffer = d::framebufferBytes(d::kMaxMode);  // 드라이버가 실제로 잡는 버퍼 (2560×1600)

bool sortedBySize(const d::ModeTable &t)
{
    for (size_t i = 1; i < t.count; ++i) {
        if (d::lessThan(t.entries[i].mode, t.entries[i - 1].mode)) {
            return false;
        }
    }
    return true;
}
} // namespace

VV_TEST(modeTable_standardPreferred_allTenModes)
{
    const auto t = d::buildModeTable({1280, 800}, kMaxBuffer);
    VV_EXPECT(t.count == d::kStandardModeCount);
    VV_EXPECT(sortedBySize(t));
    const auto *p = d::findPreferredMode(t);
    VV_EXPECT(p != nullptr && p->id == 2 && p->mode == (d::Mode{1280, 800}));
    // ID는 표 순서로 고정된다 (재부팅 간 복원에 필요).
    VV_EXPECT(d::findModeById(t, 1)->mode == (d::Mode{1024, 768}));
    VV_EXPECT(d::findModeById(t, 10)->mode == d::kMaxMode);
}

VV_TEST(modeTable_nonStandardPreferred_insertedInOrder)
{
    const auto t = d::buildModeTable({1600, 1000}, kMaxBuffer);  // GL 백엔드 실측 (WI-2.010)
    VV_EXPECT(t.count == d::kStandardModeCount + 1);
    VV_EXPECT(sortedBySize(t));
    const auto *p = d::findPreferredMode(t);
    VV_EXPECT(p != nullptr && p->id == d::kPreferredModeId && p->mode == (d::Mode{1600, 1000}));
    VV_EXPECT(d::findModeById(t, d::kPreferredModeId) == p);
}

VV_TEST(modeTable_smallBuffer_dropsLargeModes)
{
    const auto t = d::buildModeTable({1280, 800}, d::framebufferBytes({1920, 1080}));
    for (size_t i = 0; i < t.count; ++i) {
        VV_EXPECT(d::framebufferBytes(t.entries[i].mode) <= d::framebufferBytes({1920, 1080}));
    }
    VV_EXPECT(d::findModeById(t, 10) == nullptr);   // 2560×1600 빠짐
    VV_EXPECT(d::findModeById(t, 7) != nullptr);    // 1920×1080 남음
}

VV_TEST(modeTable_unusablePreferred_fallsBack)
{
    const auto t = d::buildModeTable({4000, 3000}, kMaxBuffer);
    const auto *p = d::findPreferredMode(t);
    VV_EXPECT(p != nullptr && p->mode == d::kFallbackMode);
    VV_EXPECT(d::findModeById(t, d::kPreferredModeId) == nullptr);
    VV_EXPECT(d::findModeById(t, 999) == nullptr);
}
