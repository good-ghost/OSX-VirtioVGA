// split virtqueue 드라이버 쪽 로직 (WI-1.004, REQ-013, VIRTIO 1.2 §2.7).
// 메모리는 호출자가 준다(커널: DMA 메모리, 테스트: 일반 배열). 커널 비의존이라 사용자 공간에서 테스트한다.
//
// 동시성 전제: 한 큐의 제출(add/publish)과 회수(collect)는 같은 직렬화 문맥(워크루프·명령 게이트)에서만 부른다.
// 장치와는 메모리 배리어로만 순서를 맞춘다.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "vv/VirtioSpec.hpp"

namespace vv::virtio {

// 장치와 공유하는 메모리의 쓰기·읽기 순서를 맞춘다 (§2.7.13: 디스크립터를 쓴 뒤 avail idx를 올리기 전,
// used idx를 읽은 뒤 used 원소를 읽기 전).
inline void ringMemoryBarrier()
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

struct RingLayout {
    size_t descOffset;
    size_t availOffset;
    size_t usedOffset;
    size_t totalBytes;
};

constexpr size_t alignUp(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

// 하나의 연속 메모리 블록 안에 desc·avail·used를 정렬 요구(16/2/4)대로 배치한다.
constexpr RingLayout ringLayout(uint32_t queueSize)
{
    RingLayout layout{};
    layout.descOffset = 0;
    layout.availOffset = alignUp(layout.descOffset + descTableBytes(queueSize), kAvailRingAlign);
    layout.usedOffset = alignUp(layout.availOffset + availRingBytes(queueSize), kUsedRingAlign);
    layout.totalBytes = layout.usedOffset + usedRingBytes(queueSize);
    return layout;
}

// 버퍼 한 조각. 장치가 읽는 것(out)과 쓰는 것(in)을 구분한다.
struct RingSegment {
    uint64_t address;  // 장치가 보는 DMA 주소
    uint32_t length;
    bool deviceWritable;
};

struct RingCompletion {
    uint16_t head;     // add() 때 받은 디스크립터 체인 머리
    uint32_t written;  // 장치가 쓴 바이트 수 (used elem len)
};

class SplitRing {
public:
    // memory는 ringLayout(queueSize).totalBytes 이상, 0으로 초기화되어 있어야 한다.
    bool init(void *memory, size_t memoryBytes, uint16_t queueSize)
    {
        if (!isValidQueueSize(queueSize) || memory == nullptr || memoryBytes < ringLayout(queueSize).totalBytes) {
            return false;
        }
        const RingLayout layout = ringLayout(queueSize);
        auto *base = static_cast<uint8_t *>(memory);
        mDesc = reinterpret_cast<VirtqDesc *>(base + layout.descOffset);
        mAvail = reinterpret_cast<VirtqAvailHeader *>(base + layout.availOffset);
        mAvailRing = reinterpret_cast<uint16_t *>(base + layout.availOffset + sizeof(VirtqAvailHeader));
        mUsed = reinterpret_cast<VirtqUsedHeader *>(base + layout.usedOffset);
        mUsedRing = reinterpret_cast<VirtqUsedElem *>(base + layout.usedOffset + sizeof(VirtqUsedHeader));
        mSize = queueSize;
        // 빈 디스크립터를 next로 잇는다.
        for (uint16_t i = 0; i < queueSize; ++i) {
            mDesc[i].next = static_cast<uint16_t>(i + 1);
        }
        mFreeHead = 0;
        mFreeCount = queueSize;
        mAvailIdx = 0;
        mLastUsedIdx = 0;
        return true;
    }

    uint16_t size() const { return mSize; }
    uint16_t freeCount() const { return mFreeCount; }
    // 다음 add()가 체인 머리로 쓸 디스크립터 번호. 디스크립터 하나짜리 요청의 버퍼 칸을 미리 정할 때 쓴다
    // (cursorq: 링에 올리기 전에 요청을 칸에 써 두어 장치가 쓰다 만 요청을 읽지 않게 한다, WI-4.001).
    uint16_t nextFreeHead() const { return mFreeHead; }

    // 조각들을 한 체인으로 묶어 avail 링에 올린다. 장치에 알리는 것(notify)은 호출자가 한다.
    // 반환: 성공 여부. 성공하면 headOut에 체인 머리를 남긴다 (완료 때 되돌려 받는다).
    bool add(const RingSegment *segments, uint16_t count, uint16_t &headOut)
    {
        if (count == 0 || count > mFreeCount) {
            return false;
        }
        const uint16_t head = mFreeHead;
        uint16_t index = head;
        for (uint16_t i = 0; i < count; ++i) {
            VirtqDesc &desc = mDesc[index];
            desc.addr = segments[i].address;
            desc.len = segments[i].length;
            desc.flags = segments[i].deviceWritable ? descFlag::kWrite : 0;
            if (i + 1 < count) {
                desc.flags |= descFlag::kNext;
                index = desc.next;
            }
        }
        // 체인의 마지막 디스크립터 next가 다음 빈 디스크립터를 가리킨다.
        mFreeHead = mDesc[index].next;
        mFreeCount = static_cast<uint16_t>(mFreeCount - count);

        mAvailRing[mAvailIdx % mSize] = head;
        ++mAvailIdx;
        ringMemoryBarrier();  // 디스크립터·링 원소가 idx보다 먼저 보이게 한다
        __atomic_store_n(&mAvail->idx, mAvailIdx, __ATOMIC_RELEASE);
        headOut = head;
        return true;
    }

    enum class CollectResult {
        Empty,        // 장치가 끝낸 것이 없다
        Completed,    // out에 하나를 채웠다
        DeviceError,  // 장치가 범위 밖 id를 썼다 (NFR-006). 호출자는 장치 오류로 처리해 reset한다
    };

    // 장치가 끝낸 것이 있으면 하나 꺼내 체인을 빈 목록에 되돌린다.
    CollectResult collect(RingCompletion &out)
    {
        const uint16_t usedIdx = __atomic_load_n(&mUsed->idx, __ATOMIC_ACQUIRE);
        if (usedIdx == mLastUsedIdx) {
            return CollectResult::Empty;
        }
        ringMemoryBarrier();  // idx를 본 뒤 원소를 읽는다
        const VirtqUsedElem &elem = mUsedRing[mLastUsedIdx % mSize];
        ++mLastUsedIdx;
        if (elem.id >= mSize) {
            return CollectResult::DeviceError;
        }
        const auto head = static_cast<uint16_t>(elem.id);
        out.head = head;
        out.written = elem.len;
        freeChain(head);
        return CollectResult::Completed;
    }

    // 장치가 쓴 used idx와 우리가 회수한 위치의 차이. 16비트 wrap-around를 고려한다.
    uint16_t pendingCompletions() const
    {
        return static_cast<uint16_t>(__atomic_load_n(&mUsed->idx, __ATOMIC_ACQUIRE) - mLastUsedIdx);
    }

    uint16_t availIdx() const { return mAvailIdx; }
    uint16_t lastUsedIdx() const { return mLastUsedIdx; }

    // 테스트용: 장치 쪽에서 보는 포인터
    VirtqDesc *descTable() const { return mDesc; }
    VirtqAvailHeader *availHeader() const { return mAvail; }
    uint16_t *availRing() const { return mAvailRing; }
    VirtqUsedHeader *usedHeader() const { return mUsed; }
    VirtqUsedElem *usedRing() const { return mUsedRing; }

private:
    void freeChain(uint16_t head)
    {
        uint16_t index = head;
        uint16_t freed = 1;
        // 순환 체인을 장치가 만들어 놓았을 때 무한 루프를 막는다.
        while ((mDesc[index].flags & descFlag::kNext) != 0 && freed < mSize) {
            index = mDesc[index].next;
            ++freed;
        }
        mDesc[index].next = mFreeHead;
        mFreeHead = head;
        mFreeCount = static_cast<uint16_t>(mFreeCount + freed);
    }

    VirtqDesc *mDesc = nullptr;
    VirtqAvailHeader *mAvail = nullptr;
    uint16_t *mAvailRing = nullptr;
    VirtqUsedHeader *mUsed = nullptr;
    VirtqUsedElem *mUsedRing = nullptr;
    uint16_t mSize = 0;
    uint16_t mFreeHead = 0;
    uint16_t mFreeCount = 0;
    uint16_t mAvailIdx = 0;
    uint16_t mLastUsedIdx = 0;
};

} // namespace vv::virtio
