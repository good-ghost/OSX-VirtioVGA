#include "TestMain.hpp"

#include "vv/GpuCommand.hpp"

namespace g = vv::virtio::gpu;

namespace {
constexpr uint32_t kDisplayInfoBytes = sizeof(g::RespDisplayInfo);
constexpr uint32_t type(g::CtrlType t)
{
    return static_cast<uint32_t>(t);
}
} // namespace

VV_TEST(response_displayInfoOk)
{
    VV_EXPECT(g::checkResponse(kDisplayInfoBytes, kDisplayInfoBytes, type(g::CtrlType::RespOkDisplayInfo),
                               g::CtrlType::RespOkDisplayInfo) == g::ResponseError::None);
}

VV_TEST(response_shortWrite_rejected)
{
    VV_EXPECT(g::checkResponse(0, kDisplayInfoBytes, type(g::CtrlType::RespOkDisplayInfo),
                               g::CtrlType::RespOkDisplayInfo) == g::ResponseError::TooShort);
    VV_EXPECT(g::checkResponse(sizeof(g::CtrlHdr), kDisplayInfoBytes, type(g::CtrlType::RespOkDisplayInfo),
                               g::CtrlType::RespOkDisplayInfo) == g::ResponseError::TooShort);
}

VV_TEST(response_errorTypeReported)
{
    // 오류 응답은 헤더만 온다. 기대 크기가 더 커도 TooShort가 아니라 ErrorType으로 보고해야 원인이 보인다.
    VV_EXPECT(g::checkResponse(sizeof(g::CtrlHdr), kDisplayInfoBytes, type(g::CtrlType::RespErrInvalidParameter),
                               g::CtrlType::RespOkDisplayInfo) == g::ResponseError::ErrorType);
    VV_EXPECT(g::checkResponse(sizeof(g::CtrlHdr), sizeof(g::CtrlHdr), type(g::CtrlType::RespErrUnspec),
                               g::CtrlType::RespOkNodata) == g::ResponseError::ErrorType);
}

VV_TEST(response_unexpectedOkType_rejected)
{
    VV_EXPECT(g::checkResponse(kDisplayInfoBytes, kDisplayInfoBytes, type(g::CtrlType::RespOkNodata),
                               g::CtrlType::RespOkDisplayInfo) == g::ResponseError::UnexpectedType);
}

VV_TEST(displayInfo_scanoutCountClamped)
{
    VV_EXPECT(g::usableScanouts(1) == 1);
    VV_EXPECT(g::usableScanouts(16) == 16);
    VV_EXPECT(g::usableScanouts(17) == 16);
    VV_EXPECT(g::usableScanouts(0xFFFFFFFF) == 16);
}
