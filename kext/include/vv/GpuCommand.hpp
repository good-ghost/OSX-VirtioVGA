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

// display info에서 볼 scanout 수. 장치 config의 num_scanouts를 그대로 믿지 않고 응답 배열 크기로 자른다.
constexpr uint32_t usableScanouts(uint32_t numScanouts)
{
    return numScanouts < kMaxScanouts ? numScanouts : kMaxScanouts;
}

} // namespace vv::virtio::gpu
