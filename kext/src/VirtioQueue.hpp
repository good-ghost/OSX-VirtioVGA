// virtqueue 하나 (WI-1.004). DMA 메모리와 SplitRing 로직을 묶는다. 장치 등록은 VirtioPciTransport::setupQueue가 한다.
#pragma once

#include <IOKit/IOService.h>

#include "vv/SplitRing.hpp"

class VVDmaBuffer;

class VirtioQueue : public OSObject {
    OSDeclareDefaultStructors(VirtioQueue)

public:
    // size는 2의 거듭제곱. index는 장치의 큐 번호.
    static VirtioQueue *withSize(uint16_t index, uint16_t size);

    uint16_t index() const { return mIndex; }
    uint16_t size() const { return mRing.size(); }
    vv::virtio::SplitRing &ring() { return mRing; }

    // 장치에 알려 줄 각 영역의 DMA 주소 (§4.1.4.3 queue_desc/queue_driver/queue_device)
    uint64_t descAddress() const;
    uint64_t availAddress() const;
    uint64_t usedAddress() const;

    // 큐 알림 오프셋 (setupQueue가 정한다)
    void setNotifyOffset(uint32_t offset) { mNotifyOffset = offset; }
    uint32_t notifyOffset() const { return mNotifyOffset; }

    void free() override;

private:
    bool initWithSize(uint16_t index, uint16_t size);

    VVDmaBuffer *mMemory = nullptr;
    vv::virtio::SplitRing mRing;
    vv::virtio::RingLayout mLayout{};
    uint16_t mIndex = 0;
    uint32_t mNotifyOffset = 0;
};
