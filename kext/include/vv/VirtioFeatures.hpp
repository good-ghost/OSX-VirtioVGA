// virtio-gpu feature 협상 규칙 (WI-1.003, REQ-012, REQ-027).
// 커널 비의존 순수 로직이라 사용자 공간 단위 테스트로 검증한다.
#pragma once

#include <stdint.h>

#include "vv/VirtioGpuSpec.hpp"
#include "vv/VirtioSpec.hpp"

namespace vv::virtio {

// 반드시 있어야 하는 feature. 없으면 장치를 쓰지 않는다.
inline constexpr uint64_t kRequiredFeatures = feature::bit(feature::kVersion1);

// 장치가 제공하면 받아들이는 feature.
// - ACCESS_PLATFORM: 제공됐는데 받지 않으면 장치가 FEATURES_OK를 거절할 수 있다(§6). 우리는 DMA 주소를
//   IODMACommand로 얻으므로 IOMMU가 있어도 맞는 주소를 준다.
// - GPU EDID: REQ-025.
// INDIRECT_DESC·EVENT_IDX는 성능 측정 뒤에 정한다(REQ-016, WI-4.005). 지금은 받지 않는다.
inline constexpr uint64_t kOptionalFeatures =
    feature::bit(feature::kAccessPlatform) | feature::bit(gpu::feature::kEdid);

// 절대 받지 않는 feature (REQ-027: 3D·blob 범위 밖). 실수로 kOptionalFeatures에 들어가지 않게 따로 둔다.
inline constexpr uint64_t kForbiddenFeatures =
    feature::bit(gpu::feature::kVirgl) | feature::bit(gpu::feature::kResourceBlob) |
    feature::bit(gpu::feature::kContextInit) | feature::bit(gpu::feature::kResourceUuid);

static_assert((kRequiredFeatures & kForbiddenFeatures) == 0, "필수와 금지가 겹친다");
static_assert((kOptionalFeatures & kForbiddenFeatures) == 0, "선택과 금지가 겹친다");

enum class NegotiationError {
    None,
    MissingVersion1,
};

struct NegotiationResult {
    NegotiationError error = NegotiationError::None;
    uint64_t driverFeatures = 0;
};

constexpr NegotiationResult negotiateFeatures(uint64_t deviceFeatures)
{
    NegotiationResult result;
    if ((deviceFeatures & kRequiredFeatures) != kRequiredFeatures) {
        result.error = NegotiationError::MissingVersion1;
        return result;
    }
    result.driverFeatures = deviceFeatures & (kRequiredFeatures | kOptionalFeatures) & ~kForbiddenFeatures;
    return result;
}

constexpr const char *negotiationErrorName(NegotiationError error)
{
    switch (error) {
    case NegotiationError::None: return "none";
    case NegotiationError::MissingVersion1: return "missing-version-1";
    }
    return "unknown";
}

} // namespace vv::virtio
