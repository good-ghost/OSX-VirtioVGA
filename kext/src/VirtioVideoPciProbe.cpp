#include "VirtioVideoPciProbe.hpp"

#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSString.h>

#include "VVBootArgs.hpp"
#include "VVLog.hpp"
#include "vv/PciCapWalk.hpp"
#include "vv/VirtioGpuSpec.hpp"
#include "vv/VirtioSpec.hpp"

OSDefineMetaClassAndStructors(VirtioVideoPciProbe, IOService)

namespace {

constexpr size_t kMaxVirtioCaps = 8;
constexpr size_t kCapLineBytes = 96;
constexpr uint8_t kBarRegisterStride = 4;

uint8_t barRegister(uint8_t bar)
{
    return static_cast<uint8_t>(kIOPCIConfigBaseAddress0 + kBarRegisterStride * bar);
}

// MMIO 읽기. 컴파일러가 접근을 합치거나 없애지 않도록 volatile로 읽는다.
template <typename T>
T mmioRead(IOVirtualAddress base, uint32_t offset)
{
    return *reinterpret_cast<volatile T *>(base + offset);
}

} // namespace

IOService *VirtioVideoPciProbe::probe(IOService *provider, SInt32 *score)
{
    if (vv::isDriverDisabledByBootArg()) {
        return nullptr;
    }
    return IOService::probe(provider, score);
}

bool VirtioVideoPciProbe::start(IOService *provider)
{
    if (!IOService::start(provider)) {
        return false;
    }
    if (!vv::initLog()) {
        setProperty("VVLogInitFailed", true);
    }
    IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, provider);
    if (pci == nullptr) {
        VV_LOG_ERR("pciprobe: provider is not IOPCIDevice");
        vv::publishLog(this);
        return true;  // 진단 서비스라 실패해도 붙어 있으면서 사실만 남긴다
    }
    VV_LOG_INFO("pciprobe: start (read-only)");
    publishPciHeader(pci);
    publishMsix(pci);
    publishVirtioCaps(pci);
    vv::publishLog(this);
    return true;
}

void VirtioVideoPciProbe::stop(IOService *provider)
{
    VV_LOG_INFO("pciprobe: stop");
    vv::publishLog(this);
    IOService::stop(provider);
}

void VirtioVideoPciProbe::publishPciHeader(IOPCIDevice *pci)
{
    const uint16_t vendor = pci->configRead16(kIOPCIConfigVendorID);
    const uint16_t device = pci->configRead16(kIOPCIConfigDeviceID);
    const uint16_t command = pci->configRead16(kIOPCIConfigCommand);
    const uint16_t status = pci->configRead16(kIOPCIConfigStatus);
    const uint32_t classRev = pci->configRead32(kIOPCIConfigRevisionID);
    setProperty("VVPciVendorDevice", (static_cast<unsigned long long>(vendor) << 16) | device, 32);
    setProperty("VVPciCommand", command, 16);
    setProperty("VVPciStatus", status, 16);
    setProperty("VVPciClassRev", classRev, 32);
    VV_LOG_INFO("pciprobe: id=%04x:%04x cmd=0x%04x status=0x%04x classrev=0x%08x", vendor, device, command,
                status, classRev);
}

void VirtioVideoPciProbe::publishMsix(IOPCIDevice *pci)
{
    auto read8 = [pci](uint8_t offset) { return pci->configRead8(offset); };
    const uint8_t msix = vv::pci::findCapability(read8, vv::pci::kCapIdMsix);
    if (msix == 0) {
        VV_LOG_INFO("pciprobe: no MSI-X capability");
        return;
    }
    const uint16_t control = pci->configRead16(static_cast<uint8_t>(msix + vv::pci::kMsixControlOffset));
    setProperty("VVMsixControl", control, 16);
    VV_LOG_INFO("pciprobe: msix cap=0x%02x enabled=%d table=%u", msix,
                (control & vv::pci::kMsixControlEnable) != 0, (control & vv::pci::kMsixControlTableSizeMask) + 1u);
}

void VirtioVideoPciProbe::publishVirtioCaps(IOPCIDevice *pci)
{
    namespace v = vv::virtio;
    auto read8 = [pci](uint8_t offset) { return pci->configRead8(offset); };
    auto read32 = [pci](uint8_t offset) { return pci->configRead32(offset); };

    vv::pci::VirtioCap caps[kMaxVirtioCaps] = {};
    const size_t count = vv::pci::findVirtioCaps(read8, read32, caps, kMaxVirtioCaps);
    VV_LOG_INFO("pciprobe: %zu virtio caps", count);

    OSArray *lines = OSArray::withCapacity(static_cast<unsigned>(count));
    const vv::pci::VirtioCap *common = nullptr;
    const vv::pci::VirtioCap *device = nullptr;
    for (size_t i = 0; i < count; ++i) {
        const auto &cap = caps[i];
        IODeviceMemory *barMemory = pci->getDeviceMemoryWithRegister(barRegister(cap.bar));
        const uint64_t barSize = barMemory != nullptr ? barMemory->getLength() : 0;
        const bool within = v::pci::isCapWithinBar(cap.offset, cap.length, barSize);
        char line[kCapLineBytes];
        snprintf(line, sizeof(line), "at=0x%02x type=%u bar=%u off=0x%x len=0x%x barsize=0x%llx ok=%d mult=%u",
                 cap.capOffset, cap.cfgType, cap.bar, cap.offset, cap.length, barSize, within,
                 cap.notifyOffMultiplier);
        VV_LOG_INFO("pciprobe: cap %s", line);
        if (lines != nullptr) {
            OSString *string = OSString::withCString(line);
            if (string != nullptr) {
                lines->setObject(string);
                string->release();
            }
        }
        if (!within) {
            continue;
        }
        if (cap.cfgType == v::pci::capType::kCommonCfg && common == nullptr) {
            common = &cap;
        } else if (cap.cfgType == v::pci::capType::kDeviceCfg && device == nullptr) {
            device = &cap;
        }
    }
    if (lines != nullptr) {
        setProperty("VVVirtioCaps", lines);
        lines->release();
    }

    if (common == nullptr || common->length < sizeof(v::pci::PciCommonCfg)) {
        VV_LOG_ERR("pciprobe: common cfg not usable");
        return;
    }
    // 읽기 전용 매핑. select 레지스터에 쓰지 않으므로 device_feature는 현재 select된 32비트 절반이다.
    IOMemoryMap *map = pci->mapDeviceMemoryWithRegister(barRegister(common->bar), kIOMapInhibitCache);
    if (map == nullptr) {
        VV_LOG_ERR("pciprobe: map BAR%u failed", common->bar);
        return;
    }
    const IOVirtualAddress base = map->getVirtualAddress() + common->offset;
    using Cfg = v::pci::PciCommonCfg;
    const uint32_t featureSelect = mmioRead<uint32_t>(base, offsetof(Cfg, deviceFeatureSelect));
    const uint32_t deviceFeature = mmioRead<uint32_t>(base, offsetof(Cfg, deviceFeature));
    const uint32_t driverSelect = mmioRead<uint32_t>(base, offsetof(Cfg, driverFeatureSelect));
    const uint32_t driverFeature = mmioRead<uint32_t>(base, offsetof(Cfg, driverFeature));
    const uint16_t msixConfigVector = mmioRead<uint16_t>(base, offsetof(Cfg, configMsixVector));
    const uint16_t numQueues = mmioRead<uint16_t>(base, offsetof(Cfg, numQueues));
    const uint8_t deviceStatus = mmioRead<uint8_t>(base, offsetof(Cfg, deviceStatus));
    const uint8_t configGeneration = mmioRead<uint8_t>(base, offsetof(Cfg, configGeneration));
    const uint16_t queueSelect = mmioRead<uint16_t>(base, offsetof(Cfg, queueSelect));
    const uint16_t queueEnable = mmioRead<uint16_t>(base, offsetof(Cfg, queueEnable));

    setProperty("VVDeviceStatus", deviceStatus, 8);
    setProperty("VVDeviceFeatureSelect", featureSelect, 32);
    setProperty("VVDeviceFeature", deviceFeature, 32);
    setProperty("VVDriverFeatureSelect", driverSelect, 32);
    setProperty("VVDriverFeature", driverFeature, 32);
    setProperty("VVConfigMsixVector", msixConfigVector, 16);
    setProperty("VVNumQueues", numQueues, 16);
    setProperty("VVConfigGeneration", configGeneration, 8);
    setProperty("VVQueueSelect", queueSelect, 16);
    setProperty("VVQueueEnable", queueEnable, 16);
    VV_LOG_INFO("pciprobe: status=0x%02x devfeat[%u]=0x%08x drvfeat[%u]=0x%08x msixcfg=0x%04x nq=%u gen=%u qsel=%u qen=%u",
                deviceStatus, featureSelect, deviceFeature, driverSelect, driverFeature, msixConfigVector, numQueues,
                configGeneration, queueSelect, queueEnable);

    if (device != nullptr && device->bar == common->bar && device->length >= sizeof(v::gpu::Config)) {
        const IOVirtualAddress dev = map->getVirtualAddress() + device->offset;
        using Gpu = v::gpu::Config;
        const uint32_t eventsRead = mmioRead<uint32_t>(dev, offsetof(Gpu, eventsRead));
        const uint32_t numScanouts = mmioRead<uint32_t>(dev, offsetof(Gpu, numScanouts));
        const uint32_t numCapsets = mmioRead<uint32_t>(dev, offsetof(Gpu, numCapsets));
        setProperty("VVGpuEventsRead", eventsRead, 32);
        setProperty("VVGpuNumScanouts", numScanouts, 32);
        setProperty("VVGpuNumCapsets", numCapsets, 32);
        VV_LOG_INFO("pciprobe: gpu events=0x%x scanouts=%u capsets=%u", eventsRead, numScanouts, numCapsets);
    }
    map->release();
}
