// EDID 검증 (WI-3.003, REQ-025, NFR-006). 호스트(장치)가 준 EDID를 IOGraphics에 넘기기 전에 검사한다.
// 커널 비의존 순수 로직이라 사용자 공간 단위 테스트로 검증한다.
//
// EDID는 128바이트 블록의 나열이다. 첫 블록은 고정 헤더 00 FF FF FF FF FF FF 00으로 시작하고,
// 블록마다 128바이트 합이 0(mod 256)이 되도록 마지막 바이트가 체크섬이다 (VESA E-EDID).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace vv::edid {

inline constexpr size_t kBlockBytes = 128;
inline constexpr uint8_t kHeader[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

enum class Check {
    Ok,
    Empty,             // 크기 0
    TooLarge,          // 버퍼(장치 응답 최대 1024)보다 큼
    NotBlockMultiple,  // 128의 배수가 아님
    BadHeader,         // 첫 블록 헤더가 다름
    BadChecksum,       // 어떤 블록의 합이 0이 아님
};

constexpr const char *checkName(Check check)
{
    switch (check) {
    case Check::Ok: return "ok";
    case Check::Empty: return "empty";
    case Check::TooLarge: return "too-large";
    case Check::NotBlockMultiple: return "not-block-multiple";
    case Check::BadHeader: return "bad-header";
    case Check::BadChecksum: return "bad-checksum";
    }
    return "unknown";
}

// size: 장치가 알려 준 EDID 길이, capacity: data 버퍼 크기.
constexpr Check validate(const uint8_t *data, size_t size, size_t capacity)
{
    if (size == 0 || data == nullptr) {
        return Check::Empty;
    }
    if (size > capacity) {
        return Check::TooLarge;
    }
    if (size % kBlockBytes != 0) {
        return Check::NotBlockMultiple;
    }
    for (size_t i = 0; i < sizeof(kHeader); ++i) {
        if (data[i] != kHeader[i]) {
            return Check::BadHeader;
        }
    }
    for (size_t block = 0; block < size / kBlockBytes; ++block) {
        uint8_t sum = 0;
        for (size_t i = 0; i < kBlockBytes; ++i) {
            sum = static_cast<uint8_t>(sum + data[block * kBlockBytes + i]);
        }
        if (sum != 0) {
            return Check::BadChecksum;
        }
    }
    return Check::Ok;
}

} // namespace vv::edid
