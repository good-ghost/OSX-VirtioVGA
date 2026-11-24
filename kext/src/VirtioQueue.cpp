#include "VirtioQueue.hpp"

#include "VVDmaBuffer.hpp"
#include "VVLog.hpp"

OSDefineMetaClassAndStructors(VirtioQueue, OSObject)

VirtioQueue *VirtioQueue::withSize(uint16_t index, uint16_t size)
{
    auto *queue = OSTypeAlloc(VirtioQueue);
    if (queue == nullptr) {
        return nullptr;
    }
    if (!queue->initWithSize(index, size)) {
        queue->release();
        return nullptr;
    }
    return queue;
}

bool VirtioQueue::initWithSize(uint16_t index, uint16_t size)
{
    if (!OSObject::init() || !vv::virtio::isValidQueueSize(size)) {
        return false;
    }
    mIndex = index;
    mLayout = vv::virtio::ringLayout(size);
    mMemory = VVDmaBuffer::withLength(mLayout.totalBytes);
    if (mMemory == nullptr) {
        return false;
    }
    if (!mRing.init(mMemory->cpuAddress(), mMemory->length(), size)) {
        VV_LOG_ERR("queue %u: ring init failed", index);
        return false;
    }
    return true;
}

uint64_t VirtioQueue::descAddress() const
{
    return mMemory->deviceAddress() + mLayout.descOffset;
}

uint64_t VirtioQueue::availAddress() const
{
    return mMemory->deviceAddress() + mLayout.availOffset;
}

uint64_t VirtioQueue::usedAddress() const
{
    return mMemory->deviceAddress() + mLayout.usedOffset;
}

void VirtioQueue::free()
{
    if (mMemory != nullptr) {
        mMemory->release();
        mMemory = nullptr;
    }
    OSObject::free();
}
