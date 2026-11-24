#include "TestMain.hpp"

#include "vv/DisplayMode.hpp"
#include "vv/MemEntries.hpp"

namespace d = vv::display;
namespace g = vv::virtio::gpu;

namespace {
g::DisplayOne scanout(uint32_t w, uint32_t h, uint32_t enabled)
{
    g::DisplayOne one{};
    one.r.width = w;
    one.r.height = h;
    one.enabled = enabled;
    return one;
}
} // namespace

VV_TEST(preferred_observedDevice_used)
{
    const auto p = d::preferredModeFrom(scanout(1280, 800, 1));  // WI-1.007 실측
    VV_EXPECT(!p.fallbackUsed && p.mode == (d::Mode{1280, 800}));
}

VV_TEST(preferred_zeroOrDisabledOrTooLarge_fallsBack)
{
    VV_EXPECT(d::preferredModeFrom(scanout(0, 0, 1)).fallbackUsed);
    VV_EXPECT(d::preferredModeFrom(scanout(1920, 1080, 0)).fallbackUsed);
    VV_EXPECT(d::preferredModeFrom(scanout(3840, 2160, 1)).fallbackUsed);
    VV_EXPECT(d::preferredModeFrom(scanout(2561, 1600, 1)).fallbackUsed);
    VV_EXPECT(d::preferredModeFrom(scanout(1920, 1080, 1)).mode == (d::Mode{1920, 1080}));
    VV_EXPECT(d::preferredModeFrom(scanout(2560, 1600, 1)).mode == d::kMaxMode);
    VV_EXPECT(d::preferredModeFrom(scanout(0, 0, 1)).mode == d::kFallbackMode);
}

VV_TEST(framebuffer_sizes)
{
    VV_EXPECT(d::rowBytes({1280, 800}) == 5120);
    VV_EXPECT(d::framebufferBytes({1280, 800}) == 4096000);          // 이미 page 배수
    VV_EXPECT(d::framebufferBytes({1920, 1080}) == 8294400);         // 2025 page
    VV_EXPECT(d::framebufferBytes({2560, 1600}) == 16384000);
    VV_EXPECT(d::framebufferBytes({1, 1}) == 4096);                  // page 단위 올림
    VV_EXPECT(d::fitsInPool(d::kMaxMode));
    // 최대 모드 버퍼 2개 + 여유가 128 MiB 안에 들어간다 (Q-03 근거)
    VV_EXPECT(d::fitsInPool(d::kMaxMode, d::framebufferBytes(d::kMaxMode)));
    VV_EXPECT(!d::fitsInPool({2560, 1600}, d::kFramebufferPoolBytes - 4096));
}

VV_TEST(memEntries_contiguousMergedToOne)
{
    const g::DmaSegment segments[] = {{0x100000, 0x1000}, {0x101000, 0x2000}, {0x103000, 0x1000}};
    g::MemEntry out[4] = {};
    VV_EXPECT(g::buildMemEntries(segments, 3, out, 4) == 1);
    VV_EXPECT(out[0].addr == 0x100000 && out[0].length == 0x4000);
}

VV_TEST(memEntries_gapsKeptSeparate)
{
    const g::DmaSegment segments[] = {{0x100000, 0x1000}, {0x200000, 0x1000}};
    g::MemEntry out[4] = {};
    VV_EXPECT(g::buildMemEntries(segments, 2, out, 4) == 2);
    VV_EXPECT(out[1].addr == 0x200000);
}

VV_TEST(memEntries_invalidInputs_fail)
{
    g::MemEntry out[1] = {};
    const g::DmaSegment zero[] = {{0x100000, 0}};
    VV_EXPECT(g::buildMemEntries(zero, 1, out, 1) == 0);
    const g::DmaSegment huge[] = {{0x100000, 0x100000000ull}};
    VV_EXPECT(g::buildMemEntries(huge, 1, out, 1) == 0);
    const g::DmaSegment two[] = {{0x100000, 0x1000}, {0x300000, 0x1000}};
    VV_EXPECT(g::buildMemEntries(two, 2, out, 1) == 0);  // 칸 부족
    VV_EXPECT(g::buildMemEntries(two, 0, out, 1) == 0);  // 빈 입력
}
