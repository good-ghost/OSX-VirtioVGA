// 장치와 공유하는 물리 연속 DMA 메모리 (WI-1.004). virtqueue 링, 이후 명령 버퍼·프레임버퍼(T-01)에 쓴다.
// 한 덩어리로 할당하고 IODMACommand로 장치가 볼 주소(세그먼트 1개)를 얻는다. IOMMU가 있으면 그 주소가 매핑된 주소다.
#pragma once

#include <IOKit/IOService.h>

class IOBufferMemoryDescriptor;
class IODMACommand;

class VVDmaBuffer : public OSObject {
    OSDeclareDefaultStructors(VVDmaBuffer)

public:
    // bytes를 page 단위로 올려 할당하고 0으로 채운다. 실패하면 nullptr.
    static VVDmaBuffer *withLength(size_t bytes);

    void *cpuAddress() const { return mCpuAddress; }
    uint64_t deviceAddress() const { return mDeviceAddress; }
    // CPU 물리 주소 (IOMMU를 거치지 않은 값). IOFramebuffer의 aperture(IODeviceMemory)는 물리 주소 범위로 만든다(T-01).
    // 전체가 한 덩어리로 연속이 아니면 0.
    uint64_t physicalAddress() const { return mPhysicalAddress; }
    size_t length() const { return mLength; }

    void free() override;

private:
    bool initWithLength(size_t bytes);

    IOBufferMemoryDescriptor *mMemory = nullptr;
    IODMACommand *mDma = nullptr;
    bool mPrepared = false;
    void *mCpuAddress = nullptr;
    uint64_t mDeviceAddress = 0;
    uint64_t mPhysicalAddress = 0;
    size_t mLength = 0;
};
