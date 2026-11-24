#include "TestMain.hpp"

#include "vv/VirtioFeatures.hpp"

namespace v = vv::virtio;
using v::feature::bit;

namespace {
// WI-1.001 실측: 하위 32비트 0x30000002 (EDID, INDIRECT_DESC, EVENT_IDX). 상위는 VERSION_1을 가정.
constexpr uint64_t kObservedLow = 0x30000002;
constexpr uint64_t kObservedWithVersion1 = kObservedLow | bit(v::feature::kVersion1);
} // namespace

VV_TEST(features_observedDevice_acceptsVersion1AndEdidOnly)
{
    const auto result = v::negotiateFeatures(kObservedWithVersion1);
    VV_EXPECT(result.error == v::NegotiationError::None);
    VV_EXPECT(result.driverFeatures == (bit(v::feature::kVersion1) | bit(v::gpu::feature::kEdid)));
}

VV_TEST(features_missingVersion1_rejected)
{
    const auto result = v::negotiateFeatures(kObservedLow);
    VV_EXPECT(result.error == v::NegotiationError::MissingVersion1);
    VV_EXPECT(result.driverFeatures == 0);
}

VV_TEST(features_3dAndBlob_neverAccepted)
{
    const uint64_t offered = bit(v::feature::kVersion1) | bit(v::gpu::feature::kVirgl) |
                             bit(v::gpu::feature::kResourceBlob) | bit(v::gpu::feature::kContextInit) |
                             bit(v::gpu::feature::kResourceUuid);
    const auto result = v::negotiateFeatures(offered);
    VV_EXPECT(result.error == v::NegotiationError::None);
    VV_EXPECT((result.driverFeatures & v::kForbiddenFeatures) == 0);
    VV_EXPECT(result.driverFeatures == bit(v::feature::kVersion1));
}

VV_TEST(features_accessPlatform_acceptedWhenOffered)
{
    const auto result = v::negotiateFeatures(bit(v::feature::kVersion1) | bit(v::feature::kAccessPlatform));
    VV_EXPECT((result.driverFeatures & bit(v::feature::kAccessPlatform)) != 0);
}

VV_TEST(features_ringFeatures_notAcceptedYet)
{
    const uint64_t offered = bit(v::feature::kVersion1) | bit(v::feature::kRingIndirectDesc) |
                             bit(v::feature::kRingEventIdx) | bit(v::feature::kRingReset) |
                             bit(v::feature::kRingPacked);
    VV_EXPECT(v::negotiateFeatures(offered).driverFeatures == bit(v::feature::kVersion1));
}
