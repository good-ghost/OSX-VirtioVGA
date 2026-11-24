#include "VVDmaBuffer.hpp"

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>

#include "VVLog.hpp"

OSDefineMetaClassAndStructors(VVDmaBuffer, OSObject)

namespace {

// virtio 1.x 장치는 64비트 주소를 받는다 (queue_desc 등 le64, mem_entry addr le64).
constexpr uint8_t kDeviceAddressBits = 64;
// page 정렬이면 virtqueue 정렬 요구(16/2/4)와 이후 프레임버퍼 정렬을 모두 만족한다.
constexpr mach_vm_address_t kPageAlignedMask = ~static_cast<mach_vm_address_t>(PAGE_MASK);

} // namespace

VVDmaBuffer *VVDmaBuffer::withLength(size_t bytes)
{
    auto *buffer = OSTypeAlloc(VVDmaBuffer);
    if (buffer == nullptr) {
        return nullptr;
    }
    if (!buffer->initWithLength(bytes)) {
        buffer->release();
        return nullptr;
    }
    return buffer;
}

bool VVDmaBuffer::initWithLength(size_t bytes)
{
    if (!OSObject::init() || bytes == 0) {
        return false;
    }
    mLength = round_page(bytes);
    // 커널 kext에서 OSPtr는 보유 참조(+1)가 붙은 일반 포인터다. free()에서 release한다.
    mMemory = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous, mLength, kPageAlignedMask);
    if (mMemory == nullptr) {
        VV_LOG_ERR("dma: allocate %zu bytes contiguous failed", mLength);
        return false;
    }
    mCpuAddress = mMemory->getBytesNoCopy();
    bzero(mCpuAddress, mLength);

    mDma = IODMACommand::withSpecification(kIODMACommandOutputHost64, kDeviceAddressBits, 0,
                                           IODMACommand::kMapped, 0, 1);
    if (mDma == nullptr || mDma->setMemoryDescriptor(mMemory, false) != kIOReturnSuccess) {
        VV_LOG_ERR("dma: DMA command setup failed");
        return false;
    }
    if (mDma->prepare() != kIOReturnSuccess) {
        VV_LOG_ERR("dma: prepare failed");
        return false;
    }
    mPrepared = true;

    IODMACommand::Segment64 segment{};
    UInt32 segmentCount = 1;
    UInt64 offset = 0;
    if (mDma->gen64IOVMSegments(&offset, &segment, &segmentCount) != kIOReturnSuccess || segmentCount != 1 ||
        segment.fLength < mLength) {
        // 물리 연속으로 할당했으므로 세그먼트는 하나여야 한다.
        VV_LOG_ERR("dma: expected one segment of %zu bytes, got %u (len %llu)", mLength, segmentCount,
                   segment.fLength);
        return false;
    }
    mDeviceAddress = segment.fIOVMAddr;

    IOByteCount physicalLength = 0;
    const addr64_t physical = mMemory->getPhysicalSegment(0, &physicalLength, kIOMemoryMapperNone);
    mPhysicalAddress = physicalLength >= mLength ? physical : 0;
    return true;
}

void VVDmaBuffer::free()
{
    if (mDma != nullptr) {
        if (mPrepared) {
            mDma->complete();
        }
        mDma->clearMemoryDescriptor();
        mDma->release();
        mDma = nullptr;
    }
    if (mMemory != nullptr) {
        mMemory->release();
        mMemory = nullptr;
    }
    OSObject::free();
}
