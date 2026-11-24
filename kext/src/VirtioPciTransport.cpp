#include "VirtioPciTransport.hpp"

#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/pci/IOPCIDevice.h>

#include "VVLog.hpp"
#include "VirtioQueue.hpp"
#include "vv/PciCapWalk.hpp"

OSDefineMetaClassAndStructors(VirtioPciTransport, OSObject)

namespace {

namespace p = vv::virtio::pci;

constexpr size_t kMaxVirtioCaps = 16;
// reset 완료 대기. QEMU는 즉시 0을 돌려주지만 사양은 시간을 정하지 않는다.
constexpr uint32_t kResetTimeoutMs = 1000;
constexpr uint32_t kResetPollIntervalMs = 1;
constexpr uint32_t kFeatureWordBits = 32;
constexpr uint8_t kBarRegisterStride = 4;

uint8_t barRegister(uint8_t bar)
{
    return static_cast<uint8_t>(kIOPCIConfigBaseAddress0 + kBarRegisterStride * bar);
}

} // namespace

VirtioPciTransport *VirtioPciTransport::withDevice(IOPCIDevice *pci, uint32_t minDeviceCfgBytes,
                                                   p::LayoutError *errorOut)
{
    auto *transport = OSTypeAlloc(VirtioPciTransport);
    if (transport == nullptr) {
        return nullptr;
    }
    if (!transport->initWithDevice(pci, minDeviceCfgBytes, errorOut)) {
        transport->release();
        return nullptr;
    }
    return transport;
}

bool VirtioPciTransport::initWithDevice(IOPCIDevice *pci, uint32_t minDeviceCfgBytes, p::LayoutError *errorOut)
{
    if (!OSObject::init() || pci == nullptr) {
        return false;
    }
    mPci = pci;
    mPci->retain();

    auto read8 = [pci](uint8_t offset) { return pci->configRead8(offset); };
    auto read16 = [pci](uint8_t offset) { return pci->configRead16(offset); };
    auto read32 = [pci](uint8_t offset) { return pci->configRead32(offset); };
    if (!vv::pci::hasCapabilityList(read16)) {
        VV_LOG_ERR("transport: no PCI capability list");
        if (errorOut != nullptr) {
            *errorOut = p::LayoutError::MissingCommon;
        }
        return false;
    }

    vv::pci::VirtioCap caps[kMaxVirtioCaps] = {};
    const size_t count = vv::pci::findVirtioCaps(read8, read32, caps, kMaxVirtioCaps);

    // BAR 크기는 PCI 계층이 할당한 장치 메모리 범위로 정한다. 없는 BAR는 0.
    uint64_t barSizes[p::kBarCount] = {};
    for (uint8_t bar = 0; bar < p::kBarCount; ++bar) {
        IODeviceMemory *memory = pci->getDeviceMemoryWithRegister(barRegister(bar));
        barSizes[bar] = memory != nullptr ? memory->getLength() : 0;
    }

    const p::LayoutError error = p::buildLayout(caps, count, barSizes, minDeviceCfgBytes, mLayout);
    if (errorOut != nullptr) {
        *errorOut = error;
    }
    VV_LOG_INFO("transport: %zu caps, layout=%s", count, p::layoutErrorName(error));
    if (error != p::LayoutError::None) {
        return false;
    }
    const p::Region *regions[] = {&mLayout.common, &mLayout.notify, &mLayout.isr, &mLayout.device};
    for (const p::Region *region : regions) {
        if (!mapRegion(*region)) {
            return false;
        }
        VV_LOG_INFO("transport: region bar=%u off=0x%x len=0x%x", region->bar, region->offset, region->length);
    }
    VV_LOG_INFO("transport: notify multiplier=%u", mLayout.notifyOffMultiplier);
    return true;
}

bool VirtioPciTransport::mapRegion(const p::Region &region)
{
    if (mBarMaps[region.bar] != nullptr) {
        return true;  // 같은 BAR는 한 번만 매핑한다
    }
    // 레지스터 영역이므로 캐시하지 않는다 (BAR2가 prefetchable로 보고돼도 레지스터 의미를 지킨다).
    IOMemoryMap *map = mPci->mapDeviceMemoryWithRegister(barRegister(region.bar), kIOMapInhibitCache);
    if (map == nullptr) {
        VV_LOG_ERR("transport: map BAR%u failed", region.bar);
        return false;
    }
    // 매핑 길이가 영역 끝보다 짧으면 쓸 수 없다 (buildLayout은 PCI 계층의 BAR 크기로 검사했다).
    if (uint64_t{region.offset} + region.length > map->getLength()) {
        VV_LOG_ERR("transport: BAR%u map length 0x%llx < region end", region.bar, map->getLength());
        map->release();
        return false;
    }
    mBarMaps[region.bar] = map;
    return true;
}

volatile uint8_t *VirtioPciTransport::regionAddress(const p::Region &region, uint32_t offset, uint32_t width) const
{
    if (!region.present || uint64_t{offset} + width > region.length || mBarMaps[region.bar] == nullptr) {
        return nullptr;
    }
    const IOVirtualAddress base = mBarMaps[region.bar]->getVirtualAddress();
    return reinterpret_cast<volatile uint8_t *>(base + region.offset + offset);
}

namespace {

template <typename T>
T readAt(volatile uint8_t *address, bool *ok)
{
    if (ok != nullptr) {
        *ok = address != nullptr;
    }
    if (address == nullptr) {
        return 0;
    }
    return *reinterpret_cast<volatile T *>(address);
}

} // namespace

uint8_t VirtioPciTransport::commonRead8(uint32_t offset, bool *ok) const
{
    return readAt<uint8_t>(regionAddress(mLayout.common, offset, sizeof(uint8_t)), ok);
}

uint16_t VirtioPciTransport::commonRead16(uint32_t offset, bool *ok) const
{
    return readAt<uint16_t>(regionAddress(mLayout.common, offset, sizeof(uint16_t)), ok);
}

uint32_t VirtioPciTransport::commonRead32(uint32_t offset, bool *ok) const
{
    return readAt<uint32_t>(regionAddress(mLayout.common, offset, sizeof(uint32_t)), ok);
}

uint32_t VirtioPciTransport::deviceRead32(uint32_t offset, bool *ok) const
{
    return readAt<uint32_t>(regionAddress(mLayout.device, offset, sizeof(uint32_t)), ok);
}

void VirtioPciTransport::free()
{
    for (IOMemoryMap *&map : mBarMaps) {
        if (map != nullptr) {
            map->release();
            map = nullptr;
        }
    }
    if (mPci != nullptr) {
        mPci->release();
        mPci = nullptr;
    }
    OSObject::free();
}

namespace {

template <typename T>
bool writeAt(volatile uint8_t *address, T value)
{
    if (address == nullptr) {
        return false;
    }
    *reinterpret_cast<volatile T *>(address) = value;
    return true;
}

using CommonCfg = vv::virtio::pci::PciCommonCfg;

} // namespace

bool VirtioPciTransport::commonWrite8(uint32_t offset, uint8_t value)
{
    return writeAt<uint8_t>(regionAddress(mLayout.common, offset, sizeof(uint8_t)), value);
}

bool VirtioPciTransport::commonWrite32(uint32_t offset, uint32_t value)
{
    return writeAt<uint32_t>(regionAddress(mLayout.common, offset, sizeof(uint32_t)), value);
}

uint8_t VirtioPciTransport::status() const
{
    return commonRead8(offsetof(CommonCfg, deviceStatus));
}

bool VirtioPciTransport::reset()
{
    if (!commonWrite8(offsetof(CommonCfg, deviceStatus), 0)) {
        return false;
    }
    for (uint32_t waited = 0; waited < kResetTimeoutMs; waited += kResetPollIntervalMs) {
        if (status() == 0) {
            return true;
        }
        IOSleep(kResetPollIntervalMs);
    }
    VV_LOG_ERR("transport: reset timed out, status=0x%02x", status());
    return false;
}

bool VirtioPciTransport::addStatus(uint8_t bits)
{
    return commonWrite8(offsetof(CommonCfg, deviceStatus), static_cast<uint8_t>(status() | bits));
}

uint64_t VirtioPciTransport::readDeviceFeatures()
{
    commonWrite32(offsetof(CommonCfg, deviceFeatureSelect), 0);
    const uint64_t low = commonRead32(offsetof(CommonCfg, deviceFeature));
    commonWrite32(offsetof(CommonCfg, deviceFeatureSelect), 1);
    const uint64_t high = commonRead32(offsetof(CommonCfg, deviceFeature));
    return (high << kFeatureWordBits) | low;
}

bool VirtioPciTransport::writeDriverFeatures(uint64_t features)
{
    return commonWrite32(offsetof(CommonCfg, driverFeatureSelect), 0) &&
           commonWrite32(offsetof(CommonCfg, driverFeature), static_cast<uint32_t>(features)) &&
           commonWrite32(offsetof(CommonCfg, driverFeatureSelect), 1) &&
           commonWrite32(offsetof(CommonCfg, driverFeature), static_cast<uint32_t>(features >> kFeatureWordBits));
}

uint64_t VirtioPciTransport::readDriverFeatures()
{
    commonWrite32(offsetof(CommonCfg, driverFeatureSelect), 0);
    const uint64_t low = commonRead32(offsetof(CommonCfg, driverFeature));
    commonWrite32(offsetof(CommonCfg, driverFeatureSelect), 1);
    const uint64_t high = commonRead32(offsetof(CommonCfg, driverFeature));
    return (high << kFeatureWordBits) | low;
}

bool VirtioPciTransport::commonWrite16(uint32_t offset, uint16_t value)
{
    return writeAt<uint16_t>(regionAddress(mLayout.common, offset, sizeof(uint16_t)), value);
}

bool VirtioPciTransport::commonWrite64(uint32_t offset, uint64_t value)
{
    return commonWrite32(offset, static_cast<uint32_t>(value)) &&
           commonWrite32(offset + sizeof(uint32_t), static_cast<uint32_t>(value >> kFeatureWordBits));
}

uint16_t VirtioPciTransport::numQueues() const
{
    return commonRead16(offsetof(CommonCfg, numQueues));
}

uint16_t VirtioPciTransport::queueMaxSize(uint16_t index)
{
    if (index >= numQueues() || !commonWrite16(offsetof(CommonCfg, queueSelect), index)) {
        return 0;
    }
    return commonRead16(offsetof(CommonCfg, queueSize));
}

bool VirtioPciTransport::setupQueue(VirtioQueue *queue, uint16_t vector)
{
    if (queue == nullptr) {
        return false;
    }
    const uint16_t index = queue->index();
    const uint16_t maxSize = queueMaxSize(index);
    if (maxSize == 0 || queue->size() > maxSize) {
        VV_LOG_ERR("transport: queue %u size %u invalid (max %u)", index, queue->size(), maxSize);
        return false;
    }
    if (commonRead16(offsetof(CommonCfg, queueEnable)) != 0) {
        // reset 직후에는 모든 큐가 꺼져 있어야 한다. 켜진 큐를 다시 설정하지 않는다.
        VV_LOG_ERR("transport: queue %u already enabled", index);
        return false;
    }
    uint32_t notifyOffset = 0;
    const uint16_t notifyOff = commonRead16(offsetof(CommonCfg, queueNotifyOff));
    if (!vv::virtio::pci::notifyOffsetFor(mLayout, notifyOff, notifyOffset)) {
        VV_LOG_ERR("transport: queue %u notify_off %u outside notify region", index, notifyOff);
        return false;
    }
    // §4.1.5.1.3: 크기·벡터·주소를 쓰고 마지막에 queue_enable=1.
    if (!commonWrite16(offsetof(CommonCfg, queueMsixVector), vector) ||
        commonRead16(offsetof(CommonCfg, queueMsixVector)) != vector) {
        // §4.1.5.1.2: 장치가 벡터를 쓸 수 없으면 NO_VECTOR를 돌려준다.
        VV_LOG_ERR("transport: queue %u vector %u not accepted", index, vector);
        return false;
    }
    const bool written = commonWrite16(offsetof(CommonCfg, queueSize), queue->size()) &&
                         commonWrite64(offsetof(CommonCfg, queueDesc), queue->descAddress()) &&
                         commonWrite64(offsetof(CommonCfg, queueDriver), queue->availAddress()) &&
                         commonWrite64(offsetof(CommonCfg, queueDevice), queue->usedAddress()) &&
                         commonWrite16(offsetof(CommonCfg, queueEnable), 1);
    if (!written || commonRead16(offsetof(CommonCfg, queueEnable)) != 1) {
        VV_LOG_ERR("transport: queue %u enable failed", index);
        return false;
    }
    queue->setNotifyOffset(notifyOffset);
    VV_LOG_INFO("transport: queue %u size=%u max=%u notify_off=%u (offset 0x%x) vector=0x%x", index, queue->size(),
                maxSize, notifyOff, notifyOffset, vector);
    return true;
}

bool VirtioPciTransport::isQueueEnabled(uint16_t index)
{
    if (!commonWrite16(offsetof(CommonCfg, queueSelect), index)) {
        return false;
    }
    return commonRead16(offsetof(CommonCfg, queueEnable)) == 1;
}

bool VirtioPciTransport::deviceWrite32(uint32_t offset, uint32_t value)
{
    return writeAt<uint32_t>(regionAddress(mLayout.device, offset, sizeof(uint32_t)), value);
}

uint8_t VirtioPciTransport::readIsr()
{
    return readAt<uint8_t>(regionAddress(mLayout.isr, 0, sizeof(uint8_t)), nullptr);
}

bool VirtioPciTransport::setConfigVector(uint16_t vector)
{
    return commonWrite16(offsetof(CommonCfg, configMsixVector), vector) &&
           commonRead16(offsetof(CommonCfg, configMsixVector)) == vector;
}

bool VirtioPciTransport::notifyQueue(VirtioQueue *queue)
{
    if (queue == nullptr) {
        return false;
    }
    vv::virtio::ringMemoryBarrier();
    return writeAt<uint16_t>(regionAddress(mLayout.notify, queue->notifyOffset(), sizeof(uint16_t)), queue->index());
}

