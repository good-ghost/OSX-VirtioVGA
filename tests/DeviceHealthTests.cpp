#include "TestMain.hpp"

#include "vv/DeviceHealth.hpp"

namespace v = vv::virtio;
namespace s = vv::virtio::status;

VV_TEST(health_driverOk_isOk)
{
    VV_EXPECT(v::classifyStatus(s::kAcknowledge | s::kDriver | s::kFeaturesOk | s::kDriverOk) == v::DeviceHealth::Ok);
}

VV_TEST(health_needsResetBit_wins)
{
    const uint8_t status = s::kAcknowledge | s::kDriver | s::kFeaturesOk | s::kDriverOk | s::kDeviceNeedsReset;
    VV_EXPECT(v::classifyStatus(status) == v::DeviceHealth::NeedsReset);
    VV_EXPECT(v::classifyStatus(s::kDeviceNeedsReset) == v::DeviceHealth::NeedsReset);
}

VV_TEST(health_missingDriverOk_isLost)
{
    VV_EXPECT(v::classifyStatus(0) == v::DeviceHealth::Lost);                  // 외부 reset
    VV_EXPECT(v::classifyStatus(s::kAcknowledge) == v::DeviceHealth::Lost);    // 펌웨어·다른 드라이버가 다시 시작
    VV_EXPECT(v::classifyStatus(s::kAcknowledge | s::kDriver | s::kFeaturesOk) == v::DeviceHealth::Lost);
}

VV_TEST(health_recoveryLimit)
{
    VV_EXPECT(v::mayRecover(0));
    VV_EXPECT(v::mayRecover(v::kMaxRecoveries - 1));
    VV_EXPECT(!v::mayRecover(v::kMaxRecoveries));
}
