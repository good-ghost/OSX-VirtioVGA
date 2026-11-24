// virtio-gpu 명령 응답 검증과 display info 해석 (WI-1.007, REQ-020, REQ-023, NFR-006).
// 커널 비의존 순수 로직이라 사용자 공간 단위 테스트로 검증한다.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "vv/VirtioGpuSpec.hpp"

namespace vv::virtio::gpu {

enum class ResponseError {
    None,
    TooShort,      // 장치가 쓴 길이가 헤더보다 짧다
    ErrorType,     // RESP_ERR_* 응답
    UnexpectedType // 성공 응답이지만 기대한 종류가 아니다
};

constexpr const char *responseErrorName(ResponseError error)
{
    switch (error) {
    case ResponseError::None: return "none";
    case ResponseError::TooShort: return "too-short";
    case ResponseError::ErrorType: return "error-type";
    case ResponseError::UnexpectedType: return "unexpected-type";
    }
    return "unknown";
}

// 장치가 used 링에 적은 written 바이트와 응답 헤더 type을 검사한다.
// minBytes: 기대하는 응답 구조체 크기. 장치가 이보다 적게 썼으면 나머지는 믿을 수 없다.
// 오류 응답은 헤더만 오는 것이 정상이므로, 헤더를 읽을 수 있으면 오류 종류를 길이보다 먼저 본다.
constexpr ResponseError checkResponse(uint32_t written, uint32_t minBytes, uint32_t responseType, CtrlType expected)
{
    if (written < sizeof(CtrlHdr)) {
        return ResponseError::TooShort;
    }
    if (isErrorResponse(responseType)) {
        return ResponseError::ErrorType;
    }
    if (written < minBytes) {
        return ResponseError::TooShort;
    }
    if (responseType != static_cast<uint32_t>(expected)) {
        return ResponseError::UnexpectedType;
    }
    return ResponseError::None;
}

// 로그에 남길 명령·응답 이름 (REQ-023: 오류 시 명령 이름을 남긴다). 모르는 값은 "unknown".
constexpr const char *ctrlTypeName(uint32_t type)
{
    switch (static_cast<CtrlType>(type)) {
    case CtrlType::CmdGetDisplayInfo: return "GET_DISPLAY_INFO";
    case CtrlType::CmdResourceCreate2d: return "RESOURCE_CREATE_2D";
    case CtrlType::CmdResourceUnref: return "RESOURCE_UNREF";
    case CtrlType::CmdSetScanout: return "SET_SCANOUT";
    case CtrlType::CmdResourceFlush: return "RESOURCE_FLUSH";
    case CtrlType::CmdTransferToHost2d: return "TRANSFER_TO_HOST_2D";
    case CtrlType::CmdResourceAttachBacking: return "RESOURCE_ATTACH_BACKING";
    case CtrlType::CmdResourceDetachBacking: return "RESOURCE_DETACH_BACKING";
    case CtrlType::CmdGetCapsetInfo: return "GET_CAPSET_INFO";
    case CtrlType::CmdGetCapset: return "GET_CAPSET";
    case CtrlType::CmdGetEdid: return "GET_EDID";
    case CtrlType::CmdUpdateCursor: return "UPDATE_CURSOR";
    case CtrlType::CmdMoveCursor: return "MOVE_CURSOR";
    case CtrlType::RespOkNodata: return "OK_NODATA";
    case CtrlType::RespOkDisplayInfo: return "OK_DISPLAY_INFO";
    case CtrlType::RespOkCapsetInfo: return "OK_CAPSET_INFO";
    case CtrlType::RespOkCapset: return "OK_CAPSET";
    case CtrlType::RespOkEdid: return "OK_EDID";
    case CtrlType::RespErrUnspec: return "ERR_UNSPEC";
    case CtrlType::RespErrOutOfMemory: return "ERR_OUT_OF_MEMORY";
    case CtrlType::RespErrInvalidScanoutId: return "ERR_INVALID_SCANOUT_ID";
    case CtrlType::RespErrInvalidResourceId: return "ERR_INVALID_RESOURCE_ID";
    case CtrlType::RespErrInvalidContextId: return "ERR_INVALID_CONTEXT_ID";
    case CtrlType::RespErrInvalidParameter: return "ERR_INVALID_PARAMETER";
    }
    return "unknown";
}

// 명령 결과. 커널 쪽은 이것을 IOReturn으로 바꾼다 (VirtioGpuDeviceCommands.cpp).
enum class CommandResult {
    Ok,
    Transport,          // 보내지 못함·시간 초과 등 응답을 받기 전의 실패
    TooShort,
    UnexpectedType,
    ErrUnspec,
    ErrOutOfMemory,
    ErrInvalidScanoutId,
    ErrInvalidResourceId,
    ErrInvalidContextId,
    ErrInvalidParameter,
    ErrUnknown,         // 사양에 없는 RESP_ERR 값
};

// 응답 검사 결과와 응답 type을 합쳐 명령 결과로 바꾼다.
constexpr CommandResult commandResultFor(ResponseError check, uint32_t responseType)
{
    switch (check) {
    case ResponseError::None: return CommandResult::Ok;
    case ResponseError::TooShort: return CommandResult::TooShort;
    case ResponseError::UnexpectedType: return CommandResult::UnexpectedType;
    case ResponseError::ErrorType: break;
    }
    switch (static_cast<CtrlType>(responseType)) {
    case CtrlType::RespErrUnspec: return CommandResult::ErrUnspec;
    case CtrlType::RespErrOutOfMemory: return CommandResult::ErrOutOfMemory;
    case CtrlType::RespErrInvalidScanoutId: return CommandResult::ErrInvalidScanoutId;
    case CtrlType::RespErrInvalidResourceId: return CommandResult::ErrInvalidResourceId;
    case CtrlType::RespErrInvalidContextId: return CommandResult::ErrInvalidContextId;
    case CtrlType::RespErrInvalidParameter: return CommandResult::ErrInvalidParameter;
    default: return CommandResult::ErrUnknown;
    }
}

constexpr const char *commandResultName(CommandResult result)
{
    switch (result) {
    case CommandResult::Ok: return "ok";
    case CommandResult::Transport: return "transport";
    case CommandResult::TooShort: return "too-short";
    case CommandResult::UnexpectedType: return "unexpected-type";
    case CommandResult::ErrUnspec: return "err-unspec";
    case CommandResult::ErrOutOfMemory: return "err-out-of-memory";
    case CommandResult::ErrInvalidScanoutId: return "err-invalid-scanout-id";
    case CommandResult::ErrInvalidResourceId: return "err-invalid-resource-id";
    case CommandResult::ErrInvalidContextId: return "err-invalid-context-id";
    case CommandResult::ErrInvalidParameter: return "err-invalid-parameter";
    case CommandResult::ErrUnknown: return "err-unknown";
    }
    return "unknown";
}

// display info에서 볼 scanout 수. 장치 config의 num_scanouts를 그대로 믿지 않고 응답 배열 크기로 자른다.
constexpr uint32_t usableScanouts(uint32_t numScanouts)
{
    return numScanouts < kMaxScanouts ? numScanouts : kMaxScanouts;
}

} // namespace vv::virtio::gpu
