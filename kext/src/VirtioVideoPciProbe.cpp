#include "VirtioVideoPciProbe.hpp"

#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSString.h>

#include "VVBootArgs.hpp"
#include "VirtioPciTransport.hpp"
#include "VVLog.hpp"
#include "vv/PciCapWalk.hpp"
#include "vv/VirtioGpuSpec.hpp"
#include "vv/VirtioSpec.hpp"

OSDefineMetaClassAndStructors(VirtioVideoPciProbe, IOService)

namespace {

constexpr size_t kMaxVirtioCaps = 8;
constexpr size_t kCapLineBytes = 96;
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

    // 진단용: 장치가 보고한 capability를 거르지 않고 모두 기록한다.
    vv::pci::VirtioCap caps[kMaxVirtioCaps] = {};
    const size_t count = vv::pci::findVirtioCaps(read8, read32, caps, kMaxVirtioCaps);
    OSArray *lines = OSArray::withCapacity(static_cast<unsigned>(count));
    for (size_t i = 0; i < count && lines != nullptr; ++i) {
        const auto &cap = caps[i];
        char line[kCapLineBytes];
        snprintf(line, sizeof(line), "at=0x%02x type=%u bar=%u off=0x%x len=0x%x mult=%u", cap.capOffset,
                 cap.cfgType, cap.bar, cap.offset, cap.length, cap.notifyOffMultiplier);
        OSString *string = OSString::withCString(line);
        if (string != nullptr) {
            lines->setObject(string);
            string->release();
        }
    }
    if (lines != nullptr) {
        setProperty("VVVirtioCaps", lines);
        lines->release();
    }

    // 전송 계층으로 영역을 고르고 매핑한다 (WI-1.002). 장치에는 쓰지 않는다.
    v::pci::LayoutError error = v::pci::LayoutError::None;
    VirtioPciTransport *transport = VirtioPciTransport::withDevice(pci, sizeof(v::gpu::Config), &error);
    setProperty("VVLayoutResult", v::pci::layoutErrorName(error));
    if (transport == nullptr) {
        VV_LOG_ERR("pciprobe: transport init failed (%s)", v::pci::layoutErrorName(error));
        return;
    }
    const auto &layout = transport->layout();
    char summary[kCapLineBytes];
    snprintf(summary, sizeof(summary), "common=%u:0x%x notify=%u:0x%x isr=%u:0x%x device=%u:0x%x mult=%u",
             layout.common.bar, layout.common.offset, layout.notify.bar, layout.notify.offset, layout.isr.bar,
             layout.isr.offset, layout.device.bar, layout.device.offset, layout.notifyOffMultiplier);
    setProperty("VVLayout", summary);

    using Cfg = v::pci::PciCommonCfg;
    using Gpu = v::gpu::Config;
    // select 레지스터에 쓰지 않으므로 device_feature는 현재 select된 32비트 절반이다.
    const uint8_t deviceStatus = transport->commonRead8(offsetof(Cfg, deviceStatus));
    const uint32_t featureSelect = transport->commonRead32(offsetof(Cfg, deviceFeatureSelect));
    const uint32_t deviceFeature = transport->commonRead32(offsetof(Cfg, deviceFeature));
    const uint32_t driverFeature = transport->commonRead32(offsetof(Cfg, driverFeature));
    const uint16_t msixConfigVector = transport->commonRead16(offsetof(Cfg, configMsixVector));
    const uint16_t numQueues = transport->commonRead16(offsetof(Cfg, numQueues));
    const uint16_t queueEnable = transport->commonRead16(offsetof(Cfg, queueEnable));
    const uint32_t numScanouts = transport->deviceRead32(offsetof(Gpu, numScanouts));
    const uint32_t eventsRead = transport->deviceRead32(offsetof(Gpu, eventsRead));

    // 영역 경계 검사 확인: common 영역 끝을 넘는 읽기는 거절되어야 한다.
    bool outOfRangeOk = true;
    transport->commonRead32(layout.common.length - 2, &outOfRangeOk);
    setProperty("VVBoundsCheckRejected", !outOfRangeOk);

    setProperty("VVDeviceStatus", deviceStatus, 8);
    setProperty("VVDeviceFeatureSelect", featureSelect, 32);
    setProperty("VVDeviceFeature", deviceFeature, 32);
    setProperty("VVDriverFeature", driverFeature, 32);
    setProperty("VVConfigMsixVector", msixConfigVector, 16);
    setProperty("VVNumQueues", numQueues, 16);
    setProperty("VVQueueEnable", queueEnable, 16);
    setProperty("VVGpuNumScanouts", numScanouts, 32);
    setProperty("VVGpuEventsRead", eventsRead, 32);
    VV_LOG_INFO("pciprobe: %s", summary);
    VV_LOG_INFO("pciprobe: status=0x%02x devfeat[%u]=0x%08x drvfeat=0x%08x msixcfg=0x%04x nq=%u qen=%u scanouts=%u "
                "bounds-reject=%d",
                deviceStatus, featureSelect, deviceFeature, driverFeature, msixConfigVector, numQueues, queueEnable,
                numScanouts, !outOfRangeOk);
    transport->release();
}
