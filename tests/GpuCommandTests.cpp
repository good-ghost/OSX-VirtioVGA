#include "TestMain.hpp"

#include <string>

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

VV_TEST(names_coverAllSpecTypes)
{
    const g::CtrlType all[] = {
        g::CtrlType::CmdGetDisplayInfo, g::CtrlType::CmdResourceCreate2d, g::CtrlType::CmdResourceUnref,
        g::CtrlType::CmdSetScanout, g::CtrlType::CmdResourceFlush, g::CtrlType::CmdTransferToHost2d,
        g::CtrlType::CmdResourceAttachBacking, g::CtrlType::CmdResourceDetachBacking, g::CtrlType::CmdGetCapsetInfo,
        g::CtrlType::CmdGetCapset, g::CtrlType::CmdGetEdid, g::CtrlType::CmdUpdateCursor, g::CtrlType::CmdMoveCursor,
        g::CtrlType::RespOkNodata, g::CtrlType::RespOkDisplayInfo, g::CtrlType::RespOkCapsetInfo,
        g::CtrlType::RespOkCapset, g::CtrlType::RespOkEdid, g::CtrlType::RespErrUnspec,
        g::CtrlType::RespErrOutOfMemory, g::CtrlType::RespErrInvalidScanoutId, g::CtrlType::RespErrInvalidResourceId,
        g::CtrlType::RespErrInvalidContextId, g::CtrlType::RespErrInvalidParameter,
    };
    for (g::CtrlType t : all) {
        VV_EXPECT(std::string(g::ctrlTypeName(type(t))) != "unknown");
    }
    VV_EXPECT(std::string(g::ctrlTypeName(0x9999)) == "unknown");
    VV_EXPECT(std::string(g::ctrlTypeName(type(g::CtrlType::CmdResourceUnref))) == "RESOURCE_UNREF");
}

VV_TEST(commandResult_mapsErrorTypes)
{
    using R = g::CommandResult;
    VV_EXPECT(g::commandResultFor(g::ResponseError::None, type(g::CtrlType::RespOkNodata)) == R::Ok);
    VV_EXPECT(g::commandResultFor(g::ResponseError::TooShort, 0) == R::TooShort);
    VV_EXPECT(g::commandResultFor(g::ResponseError::UnexpectedType, type(g::CtrlType::RespOkEdid)) == R::UnexpectedType);
    VV_EXPECT(g::commandResultFor(g::ResponseError::ErrorType, type(g::CtrlType::RespErrInvalidResourceId)) ==
              R::ErrInvalidResourceId);
    VV_EXPECT(g::commandResultFor(g::ResponseError::ErrorType, type(g::CtrlType::RespErrOutOfMemory)) ==
              R::ErrOutOfMemory);
    VV_EXPECT(g::commandResultFor(g::ResponseError::ErrorType, 0x12FF) == R::ErrUnknown);
}
