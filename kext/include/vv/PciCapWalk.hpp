// PCI capability 목록 순회와 virtio vendor capability 해석 (WI-1.001 조사, WI-1.002 기반).
// 설정 공간 읽기 함수를 인자로 받아 커널과 사용자 공간 단위 테스트에서 같은 코드를 쓴다.
// 호스트가 주는 값이므로 모든 포인터·길이를 범위 검사한다 (NFR-006).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "vv/VirtioSpec.hpp"

namespace vv::pci {

// PCI 설정 공간 레이아웃 (PCI Local Bus Spec): 상태 레지스터, capability 포인터, 표준 capability 영역.
inline constexpr uint8_t kStatusRegister = 0x06;
inline constexpr uint16_t kStatusCapabilitiesList = 1u << 4;
inline constexpr uint8_t kCapabilitiesPointer = 0x34;
inline constexpr uint8_t kFirstCapabilityOffset = 0x40;  // 헤더(0x00~0x3F) 이후
inline constexpr unsigned kConfigSpaceBytes = 256;       // 표준 설정 공간
inline constexpr uint8_t kCapPointerMask = 0xFC;          // 하위 2비트는 예약
// 4바이트 정렬 capability가 0x40~0xFF에 들어갈 수 있는 최대 개수. 순환 목록 방지용 상한.
inline constexpr unsigned kMaxCapabilities = (kConfigSpaceBytes - kFirstCapabilityOffset) / 4;

inline constexpr uint8_t kCapIdMsix = 0x11;
inline constexpr uint8_t kMsixControlOffset = 2;
inline constexpr uint16_t kMsixControlEnable = 1u << 15;
inline constexpr uint16_t kMsixControlTableSizeMask = 0x07FF;

// virtio_pci_cap 바이트 위치 (VIRTIO 1.2 §4.1.4)
inline constexpr uint8_t kVirtioCapLenOffset = 2;
inline constexpr uint8_t kVirtioCapCfgTypeOffset = 3;
inline constexpr uint8_t kVirtioCapBarOffset = 4;
inline constexpr uint8_t kVirtioCapOffsetOffset = 8;
inline constexpr uint8_t kVirtioCapLengthOffset = 12;
inline constexpr uint8_t kVirtioNotifyMultiplierOffset = 16;
inline constexpr uint8_t kVirtioCapMinLen = 16;
inline constexpr uint8_t kVirtioNotifyCapMinLen = 20;

struct VirtioCap {
    uint8_t capOffset;            // 설정 공간에서의 위치
    uint8_t cfgType;
    uint8_t bar;
    uint32_t offset;
    uint32_t length;
    uint32_t notifyOffMultiplier;  // cfgType == notify일 때만 의미가 있다
};

// 읽기 함수 형태: Read8 = uint8_t(uint8_t offset), Read16 = uint16_t(uint8_t), Read32 = uint32_t(uint8_t)
template <typename Read16>
bool hasCapabilityList(Read16 &&read16)
{
    return (read16(kStatusRegister) & kStatusCapabilitiesList) != 0;
}

// capability 목록을 순회하며 visit(capOffset, capId)를 부른다. visit가 false를 돌려주면 멈춘다.
// 순환 목록과 범위를 벗어난 포인터는 거기서 순회를 멈춘다. 반환: 방문한 capability 수.
template <typename Read8, typename Visit>
unsigned walkCapabilities(Read8 &&read8, Visit &&visit)
{
    bool seen[kConfigSpaceBytes / 4] = {};
    uint8_t pointer = read8(kCapabilitiesPointer) & kCapPointerMask;
    unsigned visited = 0;
    while (pointer >= kFirstCapabilityOffset && visited < kMaxCapabilities) {
        if (seen[pointer / 4]) {
            break;
        }
        seen[pointer / 4] = true;
        const uint8_t id = read8(pointer);
        const uint8_t next = read8(static_cast<uint8_t>(pointer + 1)) & kCapPointerMask;
        ++visited;
        if (!visit(pointer, id)) {
            break;
        }
        pointer = next;
    }
    return visited;
}

// 같은 ID의 첫 capability 위치. 없으면 0.
template <typename Read8>
uint8_t findCapability(Read8 &&read8, uint8_t capId)
{
    uint8_t found = 0;
    walkCapabilities(read8, [&](uint8_t offset, uint8_t id) {
        if (id == capId) {
            found = offset;
            return false;
        }
        return true;
    });
    return found;
}

// virtio vendor capability를 out에 최대 maxCaps개 채운다. 반환: 찾은 개수(유효한 것만).
// cap_len이 구조체보다 짧거나, 구조체가 설정 공간 끝을 넘거나, BAR 번호가 5를 넘으면 버린다.
template <typename Read8, typename Read32>
size_t findVirtioCaps(Read8 &&read8, Read32 &&read32, VirtioCap *out, size_t maxCaps)
{
    size_t count = 0;
    walkCapabilities(read8, [&](uint8_t offset, uint8_t id) {
        if (id != virtio::pci::kCapIdVendorSpecific) {
            return true;
        }
        const uint8_t capLen = read8(static_cast<uint8_t>(offset + kVirtioCapLenOffset));
        if (capLen < kVirtioCapMinLen || unsigned{offset} + capLen > kConfigSpaceBytes) {
            return true;
        }
        const uint8_t bar = read8(static_cast<uint8_t>(offset + kVirtioCapBarOffset));
        if (bar > virtio::pci::kMaxBarIndex) {
            return true;
        }
        VirtioCap cap{};
        cap.capOffset = offset;
        cap.cfgType = read8(static_cast<uint8_t>(offset + kVirtioCapCfgTypeOffset));
        cap.bar = bar;
        cap.offset = read32(static_cast<uint8_t>(offset + kVirtioCapOffsetOffset));
        cap.length = read32(static_cast<uint8_t>(offset + kVirtioCapLengthOffset));
        if (cap.cfgType == virtio::pci::capType::kNotifyCfg && capLen >= kVirtioNotifyCapMinLen) {
            cap.notifyOffMultiplier = read32(static_cast<uint8_t>(offset + kVirtioNotifyMultiplierOffset));
        }
        if (count < maxCaps) {
            out[count++] = cap;
            return true;
        }
        return false;
    });
    return count;
}

} // namespace vv::pci
