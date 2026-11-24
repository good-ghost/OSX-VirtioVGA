#include "TestMain.hpp"

#include "vv/VirtioGpuSpec.hpp"
#include "vv/VirtioSpec.hpp"

namespace v = vv::virtio;
namespace g = vv::virtio::gpu;

VV_TEST(virtio_featureBits_matchSpec)
{
    VV_EXPECT(v::feature::bit(v::feature::kVersion1) == (uint64_t{1} << 32));
    VV_EXPECT(v::feature::bit(v::feature::kRingIndirectDesc) == (uint64_t{1} << 28));
    VV_EXPECT(v::feature::bit(v::feature::kRingEventIdx) == (uint64_t{1} << 29));
}

VV_TEST(virtio_queueSize_powerOfTwoUpTo32768)
{
    VV_EXPECT(!v::isValidQueueSize(0));
    VV_EXPECT(v::isValidQueueSize(1));
    VV_EXPECT(v::isValidQueueSize(256));
    VV_EXPECT(!v::isValidQueueSize(255));
    VV_EXPECT(v::isValidQueueSize(32768));
    VV_EXPECT(!v::isValidQueueSize(65536));
}

VV_TEST(virtio_ringBytes_matchSpecTable)
{
    // §2.7 표: desc 16*N, avail 6+2*N, used 6+8*N
    VV_EXPECT(v::descTableBytes(256) == 4096);
    VV_EXPECT(v::availRingBytes(256) == 518);
    VV_EXPECT(v::usedRingBytes(256) == 2054);
}

VV_TEST(virtio_capWithinBar_rejectsOverflowAndOutOfRange)
{
    constexpr uint64_t kBar2Size = 16 * 1024;  // 실측 BAR2 16 KiB (REQUIREMENT §2.2)
    VV_EXPECT(v::pci::isCapWithinBar(0, 0x1000, kBar2Size));
    VV_EXPECT(v::pci::isCapWithinBar(0x3000, 0x1000, kBar2Size));
    VV_EXPECT(!v::pci::isCapWithinBar(0x3000, 0x1001, kBar2Size));
    VV_EXPECT(!v::pci::isCapWithinBar(0, 0, kBar2Size));
    VV_EXPECT(!v::pci::isCapWithinBar(0xFFFFFFFFu, 0xFFFFFFFFu, kBar2Size));
}

VV_TEST(gpu_ids_matchObservedDevice)
{
    // 실측 장치 1af4:1050 (REQUIREMENT §2.2)
    VV_EXPECT(v::pci::kVendorId == 0x1AF4);
    VV_EXPECT(g::kPciDeviceId == 0x1050);
}

VV_TEST(gpu_ctrlTypes_matchSpecSequence)
{
    VV_EXPECT(static_cast<uint32_t>(g::CtrlType::CmdGetEdid) == 0x010A);
    VV_EXPECT(static_cast<uint32_t>(g::CtrlType::CmdMoveCursor) == 0x0301);
    VV_EXPECT(static_cast<uint32_t>(g::CtrlType::RespOkEdid) == 0x1104);
    VV_EXPECT(static_cast<uint32_t>(g::CtrlType::RespErrInvalidParameter) == 0x1205);
}

VV_TEST(gpu_responseClassification)
{
    VV_EXPECT(g::isOkResponse(static_cast<uint32_t>(g::CtrlType::RespOkNodata)));
    VV_EXPECT(g::isOkResponse(static_cast<uint32_t>(g::CtrlType::RespOkDisplayInfo)));
    VV_EXPECT(!g::isOkResponse(static_cast<uint32_t>(g::CtrlType::RespErrUnspec)));
    VV_EXPECT(g::isErrorResponse(static_cast<uint32_t>(g::CtrlType::RespErrInvalidParameter)));
    VV_EXPECT(!g::isErrorResponse(static_cast<uint32_t>(g::CtrlType::CmdGetDisplayInfo)));
    VV_EXPECT(!g::isErrorResponse(0x1206));
}

VV_TEST(gpu_format_bgrx_isTwo)
{
    // PHASE-PLAN-02 초안의 "B8G8R8X8 = 1"은 틀렸다. 사양상 1은 B8G8R8A8이다.
    VV_EXPECT(static_cast<uint32_t>(g::Format::B8G8R8A8Unorm) == 1);
    VV_EXPECT(static_cast<uint32_t>(g::Format::B8G8R8X8Unorm) == 2);
}
