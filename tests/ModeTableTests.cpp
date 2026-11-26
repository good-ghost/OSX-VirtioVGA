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

// WI-6.001: 창 크기 자동 전환. 동적 모드는 ID 하나(100)만 쓰고 창 크기가 바뀌면 크기만 바뀐다.
VV_TEST(modeTable_dynamicCurrent_newSizeReusesId)
{
    const d::ModeEntry current{d::kPreferredModeId, {1888, 1061}, true};
    const auto t = d::buildModeTable({1492, 889}, kMaxBuffer, &current);  // 0.0.46 실측 창 크기
    VV_EXPECT(t.count == d::kStandardModeCount + 1);
    VV_EXPECT(sortedBySize(t));
    const auto *p = d::findPreferredMode(t);
    VV_EXPECT(p != nullptr && p->id == d::kPreferredModeId && p->mode == (d::Mode{1492, 889}));
}

VV_TEST(modeTable_dynamicCurrent_standardPreferred_keepsCurrent)
{
    const d::ModeEntry current{d::kPreferredModeId, {1888, 1061}, true};
    const auto t = d::buildModeTable({1920, 1080}, kMaxBuffer, &current);
    VV_EXPECT(t.count == d::kStandardModeCount + 1);
    const auto *kept = d::findModeById(t, d::kPreferredModeId);
    VV_EXPECT(kept != nullptr && kept->mode == (d::Mode{1888, 1061}) && !kept->preferred);
    VV_EXPECT(d::findPreferredMode(t)->id == 7);
}

VV_TEST(modeTable_standardCurrent_standardPreferred_noDynamic)
{
    const d::ModeEntry current{2, {1280, 800}, true};
    const auto t = d::buildModeTable({1920, 1080}, kMaxBuffer, &current);
    VV_EXPECT(t.count == d::kStandardModeCount);
    VV_EXPECT(d::findModeById(t, d::kPreferredModeId) == nullptr);
}

VV_TEST(modeTable_oddWidth_keptExactly)
{
    // EDID 선호 타이밍과 같아야 WindowServer가 그 모드를 고른다 (0.0.45 실측).
    const auto t = d::buildModeTable({1729, 956}, kMaxBuffer);
    const auto *p = d::findPreferredMode(t);
    VV_EXPECT(p != nullptr && p->mode == (d::Mode{1729, 956}));
}
