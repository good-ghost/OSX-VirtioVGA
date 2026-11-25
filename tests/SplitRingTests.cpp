#include "TestMain.hpp"

#include <vector>

#include "vv/SplitRing.hpp"

namespace v = vv::virtio;
using Result = v::SplitRing::CollectResult;

namespace {

// 장치 흉내: avail 링에서 체인을 꺼내 used 링에 돌려준다.
struct FakeDevice {
    v::SplitRing &ring;
    uint16_t lastAvail = 0;
    uint16_t usedIdx = 0;

    // 새 avail 항목 하나를 처리해 used에 올린다. 처리한 체인 길이를 돌려준다(없으면 0).
    unsigned completeOne(uint32_t written)
    {
        const uint16_t availIdx = ring.availHeader()->idx;
        if (availIdx == lastAvail) {
            return 0;
        }
        const uint16_t head = ring.availRing()[lastAvail % ring.size()];
        ++lastAvail;
        unsigned chain = 1;
        uint16_t index = head;
        while (ring.descTable()[index].flags & v::descFlag::kNext) {
            index = ring.descTable()[index].next;
            ++chain;
        }
        auto &elem = ring.usedRing()[usedIdx % ring.size()];
        elem.id = head;
        elem.len = written;
        ++usedIdx;
        ring.usedHeader()->idx = usedIdx;
        return chain;
    }
};

struct RingFixture {
    std::vector<uint8_t> memory;
    v::SplitRing ring;
    explicit RingFixture(uint16_t size) : memory(v::ringLayout(size).totalBytes, 0)
    {
        ring.init(memory.data(), memory.size(), size);
    }
};

} // namespace

VV_TEST(ring_layout_respectsAlignment)
{
    const auto layout = v::ringLayout(64);
    VV_EXPECT(layout.descOffset % v::kDescTableAlign == 0);
    VV_EXPECT(layout.availOffset % v::kAvailRingAlign == 0);
    VV_EXPECT(layout.usedOffset % v::kUsedRingAlign == 0);
    VV_EXPECT(layout.availOffset == 64 * 16);
    VV_EXPECT(layout.usedOffset >= layout.availOffset + v::availRingBytes(64));
    VV_EXPECT(layout.totalBytes == layout.usedOffset + v::usedRingBytes(64));
}

VV_TEST(ring_init_rejectsBadSizeOrSmallMemory)
{
    std::vector<uint8_t> memory(v::ringLayout(64).totalBytes, 0);
    v::SplitRing ring;
    VV_EXPECT(!ring.init(memory.data(), memory.size(), 63));
    VV_EXPECT(!ring.init(memory.data(), memory.size() - 1, 64));
    VV_EXPECT(!ring.init(nullptr, memory.size(), 64));
    VV_EXPECT(ring.init(memory.data(), memory.size(), 64));
    VV_EXPECT(ring.freeCount() == 64);
}

VV_TEST(ring_addCollect_requestResponseChain)
{
    RingFixture f(16);
    FakeDevice device{f.ring};
    const v::RingSegment segments[] = {{0x1000, 24, false}, {0x2000, 408, true}};
    uint16_t head = 0xFFFF;
    VV_EXPECT(f.ring.add(segments, 2, head));
    VV_EXPECT(f.ring.freeCount() == 14);
    VV_EXPECT(f.ring.availHeader()->idx == 1);

    const auto &first = f.ring.descTable()[head];
    VV_EXPECT(first.addr == 0x1000 && first.len == 24 && first.flags == v::descFlag::kNext);
    const auto &second = f.ring.descTable()[first.next];
    VV_EXPECT(second.addr == 0x2000 && second.flags == v::descFlag::kWrite);

    v::RingCompletion done{};
    VV_EXPECT(f.ring.collect(done) == Result::Empty);
    VV_EXPECT(device.completeOne(408) == 2);
    VV_EXPECT(f.ring.collect(done) == Result::Completed);
    VV_EXPECT(done.head == head && done.written == 408);
    VV_EXPECT(f.ring.freeCount() == 16);
    VV_EXPECT(f.ring.collect(done) == Result::Empty);
}

VV_TEST(ring_full_rejectsUntilCompleted)
{
    RingFixture f(4);
    FakeDevice device{f.ring};
    const v::RingSegment two[] = {{0x1000, 8, false}, {0x2000, 8, true}};
    uint16_t head = 0;
    VV_EXPECT(f.ring.add(two, 2, head));
    VV_EXPECT(f.ring.add(two, 2, head));
    VV_EXPECT(!f.ring.add(two, 2, head));  // 디스크립터 4개 모두 사용 중
    VV_EXPECT(!f.ring.add(two, 0, head));
    device.completeOne(8);
    v::RingCompletion done{};
    VV_EXPECT(f.ring.collect(done) == Result::Completed);
    VV_EXPECT(f.ring.add(two, 2, head));
}

VV_TEST(ring_indexWrapAround_over65536Requests)
{
    // 16비트 avail/used idx가 65535 → 0으로 넘어가도 계속 동작해야 한다.
    RingFixture f(8);
    FakeDevice device{f.ring};
    const v::RingSegment one[] = {{0x1000, 24, false}};
    constexpr unsigned kRounds = 70000;
    unsigned completed = 0;
    bool ok = true;
    for (unsigned i = 0; i < kRounds && ok; ++i) {
        uint16_t head = 0;
        ok = f.ring.add(one, 1, head) && device.completeOne(i) == 1;
        v::RingCompletion done{};
        ok = ok && f.ring.collect(done) == Result::Completed && done.head == head && done.written == i;
        completed += ok ? 1 : 0;
    }
    VV_EXPECT(ok);
    VV_EXPECT(completed == kRounds);
    VV_EXPECT(f.ring.availIdx() == static_cast<uint16_t>(kRounds));
    VV_EXPECT(f.ring.lastUsedIdx() == static_cast<uint16_t>(kRounds));
    VV_EXPECT(f.ring.freeCount() == 8);
}

VV_TEST(ring_pendingCompletions_handlesWrap)
{
    RingFixture f(8);
    FakeDevice device{f.ring};
    const v::RingSegment one[] = {{0x1000, 24, false}};
    // 65534번 왕복해 인덱스를 wrap 직전으로 만든 뒤, 3개를 쌓는다 (65534, 65535, 0).
    for (unsigned i = 0; i < 65534; ++i) {
        uint16_t head = 0;
        f.ring.add(one, 1, head);
        device.completeOne(0);
        v::RingCompletion done{};
        f.ring.collect(done);
    }
    for (int i = 0; i < 3; ++i) {
        uint16_t head = 0;
        f.ring.add(one, 1, head);
        device.completeOne(0);
    }
    VV_EXPECT(f.ring.pendingCompletions() == 3);
}

VV_TEST(ring_outOfRangeUsedId_reportedAsDeviceError)
{
    RingFixture f(8);
    const v::RingSegment one[] = {{0x1000, 24, false}};
    uint16_t head = 0;
    f.ring.add(one, 1, head);
    f.ring.usedRing()[0].id = 8;  // 큐 크기 이상
    f.ring.usedHeader()->idx = 1;
    v::RingCompletion done{};
    VV_EXPECT(f.ring.collect(done) == Result::DeviceError);
}

VV_TEST(ring_nextFreeHead_predictsSingleDescriptorHead)
{
    // cursorq는 add 전에 요청을 칸[nextFreeHead]에 쓴다. 완료 순서가 뒤바뀌어도 예측이 맞아야 한다.
    RingFixture fixture(4);
    FakeDevice device{fixture.ring};
    const v::RingSegment segment{0x1000, 56, false};
    for (int round = 0; round < 10; ++round) {
        const uint16_t predicted = fixture.ring.nextFreeHead();
        uint16_t head = 0xFFFF;
        VV_EXPECT(fixture.ring.add(&segment, 1, head));
        VV_EXPECT(head == predicted);
        if (round % 3 == 2) {
            // 몇 개를 쌓아 두었다가 한꺼번에 돌려받는다.
            while (device.completeOne(0) != 0) {
            }
            v::RingCompletion completion{};
            while (fixture.ring.collect(completion) == Result::Completed) {
            }
        }
    }
}
