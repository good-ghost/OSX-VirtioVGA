#include "TestMain.hpp"

#include "vv/VirtioGpuSpec.hpp"
#include "vv/VirtioPciLayout.hpp"

namespace p = vv::virtio::pci;
using vv::pci::VirtioCap;

namespace {

constexpr uint32_t kGpuCfgBytes = sizeof(vv::virtio::gpu::Config);

// WI-1.001 실측 (QEMU 10.0.13 virtio-vga): BAR0 8 MiB, BAR2 16 KiB
constexpr uint64_t kObservedBars[p::kBarCount] = {0x800000, 0, 0x4000, 0, 0x1000, 0};

constexpr VirtioCap kObservedCaps[] = {
    {0xc8, p::capType::kPciCfg, 0, 0, 0, 0},
    {0xb4, p::capType::kNotifyCfg, 2, 0x3000, 0x1000, 4},
    {0xa4, p::capType::kDeviceCfg, 2, 0x2000, 0x1000, 0},
    {0x94, p::capType::kIsrCfg, 2, 0x1800, 0x800, 0},
    {0x84, p::capType::kCommonCfg, 2, 0x1000, 0x800, 0},
};

} // namespace

VV_TEST(layout_observedDevice_allRegionsInBar2)
{
    p::Layout layout;
    const auto error = p::buildLayout(kObservedCaps, 5, kObservedBars, kGpuCfgBytes, layout);
    VV_EXPECT(error == p::LayoutError::None);
    VV_EXPECT(layout.common.present && layout.common.bar == 2 && layout.common.offset == 0x1000);
    VV_EXPECT(layout.isr.offset == 0x1800 && layout.device.offset == 0x2000 && layout.notify.offset == 0x3000);
    VV_EXPECT(layout.notifyOffMultiplier == 4);
}

VV_TEST(layout_missingCommon_reported)
{
    p::Layout layout;
    VV_EXPECT(p::buildLayout(kObservedCaps, 4, kObservedBars, kGpuCfgBytes, layout) == p::LayoutError::MissingCommon);
}

VV_TEST(layout_capOutsideBar_skippedThenNextUsed)
{
    const VirtioCap caps[] = {
        {0x40, p::capType::kCommonCfg, 2, 0x3C00, 0x800, 0},  // 끝 0x4400 > 0x4000: 건너뜀
        {0x50, p::capType::kCommonCfg, 2, 0x1000, 0x800, 0},  // 이것을 쓴다
        {0x60, p::capType::kNotifyCfg, 2, 0x3000, 0x1000, 4},
        {0x70, p::capType::kIsrCfg, 2, 0x1800, 0x800, 0},
        {0x80, p::capType::kDeviceCfg, 2, 0x2000, 0x1000, 0},
    };
    p::Layout layout;
    VV_EXPECT(p::buildLayout(caps, 5, kObservedBars, kGpuCfgBytes, layout) == p::LayoutError::None);
    VV_EXPECT(layout.common.offset == 0x1000);
}

VV_TEST(layout_tooShortRegions_rejected)
{
    const VirtioCap caps[] = {
        {0x40, p::capType::kCommonCfg, 2, 0x1000, 55, 0},  // 56바이트 미만
        {0x60, p::capType::kNotifyCfg, 2, 0x3000, 0x1000, 4},
        {0x70, p::capType::kIsrCfg, 2, 0x1800, 0x800, 0},
        {0x80, p::capType::kDeviceCfg, 2, 0x2000, kGpuCfgBytes - 1, 0},
    };
    p::Layout layout;
    VV_EXPECT(p::buildLayout(caps, 4, kObservedBars, kGpuCfgBytes, layout) == p::LayoutError::MissingCommon);
}

VV_TEST(layout_barMissing_rejected)
{
    const uint64_t noBar2[p::kBarCount] = {0x800000, 0, 0, 0, 0, 0};
    p::Layout layout;
    VV_EXPECT(p::buildLayout(kObservedCaps, 5, noBar2, kGpuCfgBytes, layout) == p::LayoutError::MissingCommon);
}

VV_TEST(layout_notifyMultiplier_rules)
{
    VV_EXPECT(p::isValidNotifyMultiplier(0));
    VV_EXPECT(p::isValidNotifyMultiplier(2));
    VV_EXPECT(p::isValidNotifyMultiplier(4));
    VV_EXPECT(p::isValidNotifyMultiplier(4096));
    VV_EXPECT(!p::isValidNotifyMultiplier(1));
    VV_EXPECT(!p::isValidNotifyMultiplier(3));
    VV_EXPECT(!p::isValidNotifyMultiplier(6));

    VirtioCap caps[5];
    for (int i = 0; i < 5; ++i) {
        caps[i] = kObservedCaps[i];
    }
    caps[1].notifyOffMultiplier = 3;
    p::Layout layout;
    VV_EXPECT(p::buildLayout(caps, 5, kObservedBars, kGpuCfgBytes, layout) == p::LayoutError::BadNotifyMultiplier);
}

VV_TEST(layout_notifyOffset_boundsChecked)
{
    p::Layout layout;
    p::buildLayout(kObservedCaps, 5, kObservedBars, kGpuCfgBytes, layout);
    uint32_t offset = 0;
    VV_EXPECT(p::notifyOffsetFor(layout, 0, offset) && offset == 0);
    VV_EXPECT(p::notifyOffsetFor(layout, 1, offset) && offset == 4);
    VV_EXPECT(p::notifyOffsetFor(layout, 1023, offset) && offset == 4092);  // 마지막 2바이트까지 들어간다
    VV_EXPECT(!p::notifyOffsetFor(layout, 1024, offset));                   // 0x1000 = 영역 끝
}
