// RESOURCE_ATTACH_BACKING에 보낼 메모리 항목 목록 만들기 (WI-2.003, VIRTIO §5.7.6).
// DMA 세그먼트 목록을 받아 이어지는 것은 합치고, 개수·길이를 검사한다. 커널 비의존이라 단위 테스트한다.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "vv/VirtioGpuSpec.hpp"

namespace vv::virtio::gpu {

struct DmaSegment {
    uint64_t address;
    uint64_t length;
};

// virtio_gpu_mem_entry.length는 32비트다. 4 GiB 이상 세그먼트는 나눠야 하지만 프레임버퍼는 최대 16 MiB라
// 그런 세그먼트는 오류로 본다.
inline constexpr uint64_t kMaxMemEntryLength = 0xFFFFFFFFull;

// out에 최대 maxEntries개를 채우고 개수를 돌려준다. 0이면 실패(빈 입력, 길이 0, 너무 긴 세그먼트, 칸 부족).
inline size_t buildMemEntries(const DmaSegment *segments, size_t count, MemEntry *out, size_t maxEntries)
{
    size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        const DmaSegment &segment = segments[i];
        if (segment.length == 0 || segment.length > kMaxMemEntryLength) {
            return 0;
        }
        // 앞 항목과 물리적으로 이어지면 합친다 (합친 길이도 32비트 안이어야 한다).
        if (used > 0) {
            MemEntry &last = out[used - 1];
            const uint64_t merged = uint64_t{last.length} + segment.length;
            if (last.addr + last.length == segment.address && merged <= kMaxMemEntryLength) {
                last.length = static_cast<uint32_t>(merged);
                continue;
            }
        }
        if (used == maxEntries) {
            return 0;
        }
        out[used].addr = segment.address;
        out[used].length = static_cast<uint32_t>(segment.length);
        out[used].padding = 0;
        ++used;
    }
    return used;
}

} // namespace vv::virtio::gpu
