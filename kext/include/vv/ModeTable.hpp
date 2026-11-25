// 디스플레이 모드 목록 (WI-3.001, WI-3.004, REQ-040, REQ-041, Q-02).
// 커널 비의존 순수 로직이라 사용자 공간 단위 테스트로 검증한다.
//
// 모드 ID는 재부팅해도 같아야 한다 (macOS가 마지막 모드를 ID로 저장·복원, WI-3.007).
// 그래서 표준 모드는 표의 순서로 고정 ID(1부터)를 주고, 표에 없는 선호 해상도만 kPreferredModeId를 쓴다.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "vv/DisplayMode.hpp"

namespace vv::display {

// REQ-040 표준 모드. 순서가 곧 ID다 (바꾸지 않는다. 추가는 끝에만).
inline constexpr Mode kStandardModes[] = {
    {1024, 768},  {1280, 800},  {1280, 1024}, {1440, 900},  {1600, 900},
    {1680, 1050}, {1920, 1080}, {1920, 1200}, {2560, 1440}, {2560, 1600},
};
inline constexpr size_t kStandardModeCount = sizeof(kStandardModes) / sizeof(kStandardModes[0]);
inline constexpr uint32_t kFirstStandardModeId = 1;
// 장치 선호 해상도가 표준 모드에 없을 때(예: GL 백엔드의 창 크기 1600×1000) 쓰는 ID.
inline constexpr uint32_t kPreferredModeId = 100;
inline constexpr size_t kMaxModes = kStandardModeCount + 1;

struct ModeEntry {
    uint32_t id = 0;
    Mode mode{};
    bool preferred = false;
};

struct ModeTable {
    ModeEntry entries[kMaxModes]{};
    size_t count = 0;
};

constexpr bool fitsBuffer(const Mode &mode, uint64_t bufferBytes)
{
    return isUsable(mode) && framebufferBytes(mode) <= bufferBytes;
}

// 넓이(픽셀 수) 다음 너비 순으로 작은 것이 앞에 온다. 시스템 설정 목록이 크기 순으로 보이게 하려는 것이다.
constexpr bool lessThan(const Mode &a, const Mode &b)
{
    const uint64_t areaA = uint64_t{a.width} * a.height;
    const uint64_t areaB = uint64_t{b.width} * b.height;
    return areaA != areaB ? areaA < areaB : a.width < b.width;
}

// bufferBytes: 프레임버퍼 하나의 크기. 모든 모드가 같은 버퍼의 앞부분을 쓴다(물리 연속 재할당을 피하기 위해, T-01).
// 선호 해상도가 버퍼에 들어가지 않거나 쓸 수 없으면 kFallbackMode를 선호로 삼는다.
constexpr ModeTable buildModeTable(const Mode &preferredIn, uint64_t bufferBytes)
{
    ModeTable table{};
    const Mode preferred = fitsBuffer(preferredIn, bufferBytes) ? preferredIn : kFallbackMode;
    bool preferredIsStandard = false;
    for (size_t i = 0; i < kStandardModeCount; ++i) {
        const Mode &mode = kStandardModes[i];
        if (!fitsBuffer(mode, bufferBytes)) {
            continue;
        }
        // 크기 순서를 지키며 끼워 넣는다. ID는 표 순서로 정해져 있으므로 목록 순서와 무관하다.
        const ModeEntry entry{kFirstStandardModeId + static_cast<uint32_t>(i), mode, mode == preferred};
        preferredIsStandard = preferredIsStandard || entry.preferred;
        size_t at = table.count;
        while (at > 0 && lessThan(entry.mode, table.entries[at - 1].mode)) {
            table.entries[at] = table.entries[at - 1];
            --at;
        }
        table.entries[at] = entry;
        ++table.count;
    }
    if (!preferredIsStandard && fitsBuffer(preferred, bufferBytes)) {
        size_t at = table.count;
        while (at > 0 && lessThan(preferred, table.entries[at - 1].mode)) {
            table.entries[at] = table.entries[at - 1];
            --at;
        }
        table.entries[at] = ModeEntry{kPreferredModeId, preferred, true};
        ++table.count;
    }
    return table;
}

constexpr const ModeEntry *findModeById(const ModeTable &table, uint32_t id)
{
    for (size_t i = 0; i < table.count; ++i) {
        if (table.entries[i].id == id) {
            return &table.entries[i];
        }
    }
    return nullptr;
}

constexpr const ModeEntry *findPreferredMode(const ModeTable &table)
{
    for (size_t i = 0; i < table.count; ++i) {
        if (table.entries[i].preferred) {
            return &table.entries[i];
        }
    }
    return table.count > 0 ? &table.entries[0] : nullptr;
}

} // namespace vv::display
