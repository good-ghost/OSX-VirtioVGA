#include "VirtioGpuDevice.hpp"

#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/pci/IOPCIDevice.h>

#include "VVBootArgs.hpp"
#include "VVLog.hpp"
#include "VirtioPciTransport.hpp"
#include "vv/PciCapWalk.hpp"
#include "vv/VirtioFeatures.hpp"
#include "vv/VirtioGpuSpec.hpp"

OSDefineMetaClassAndStructors(VirtioGpuDevice, IOService)

namespace {

namespace v = vv::virtio;

// 상태 감시 주기. 외부(Apple 전송 드라이버 등)가 장치를 reset하는지 보기 위한 것이라 촘촘할 필요가 없다.
constexpr uint32_t kStatusWatchIntervalMs = 2000;

uint32_t high32(uint64_t value)
{
    return static_cast<uint32_t>(value >> 32);
}

uint32_t low32(uint64_t value)
{
    return static_cast<uint32_t>(value);
}

} // namespace

IOService *VirtioGpuDevice::probe(IOService *provider, SInt32 *score)
{
    if (vv::isDriverDisabledByBootArg()) {
        return nullptr;
    }
    return IOService::probe(provider, score);
}

bool VirtioGpuDevice::start(IOService *provider)
{
    if (!IOService::start(provider)) {
        return false;
    }
    if (!vv::initLog()) {
        setProperty("VVLogInitFailed", true);
    }
    IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, provider);
    if (pci == nullptr) {
        failDevice("provider-not-pci");
        return true;
    }
    publishPciDiagnostics(pci);

    v::pci::LayoutError layoutError = v::pci::LayoutError::None;
    mTransport = VirtioPciTransport::withDevice(pci, sizeof(v::gpu::Config), &layoutError);
    setProperty("VVLayoutResult", v::pci::layoutErrorName(layoutError));
    if (mTransport == nullptr) {
        failDevice("transport");
        return true;
    }
    if (initializeDevice()) {
        startStatusWatch();
    }
    vv::publishLog(this);
    return true;
}

bool VirtioGpuDevice::initializeDevice()
{
    VV_LOG_INFO("gpu: status before reset=0x%02x", mTransport->status());
    // §3.1.1 1단계: reset. 펌웨어나 Apple 전송 드라이버가 남긴 상태(관찰값 ACK)를 가정하지 않는다.
    if (!mTransport->reset()) {
        failDevice("reset");
        return false;
    }
    mTransport->addStatus(v::status::kAcknowledge);
    mTransport->addStatus(v::status::kDriver);

    const uint64_t deviceFeatures = mTransport->readDeviceFeatures();
    setProperty("VVDeviceFeatures", deviceFeatures, 64);
    VV_LOG_INFO("gpu: device features hi=0x%08x lo=0x%08x", high32(deviceFeatures), low32(deviceFeatures));

    const auto negotiation = v::negotiateFeatures(deviceFeatures);
    setProperty("VVNegotiation", v::negotiationErrorName(negotiation.error));
    if (negotiation.error != v::NegotiationError::None) {
        failDevice(v::negotiationErrorName(negotiation.error));
        return false;
    }
    if (!mTransport->writeDriverFeatures(negotiation.driverFeatures)) {
        failDevice("write-features");
        return false;
    }
    const uint64_t readBack = mTransport->readDriverFeatures();
    setProperty("VVDriverFeatures", readBack, 64);
    VV_LOG_INFO("gpu: driver features hi=0x%08x lo=0x%08x (wanted lo=0x%08x)", high32(readBack), low32(readBack),
                low32(negotiation.driverFeatures));

    mTransport->addStatus(v::status::kFeaturesOk);
    // §3.1.1 6단계: FEATURES_OK를 다시 읽어 장치가 feature 조합을 받아들였는지 확인한다.
    const uint8_t status = mTransport->status();
    if ((status & v::status::kFeaturesOk) == 0) {
        failDevice("features-not-accepted");
        return false;
    }
    mExpectedStatus = status;
    setProperty("VVInitResult", "features-ok");
    setProperty("VVDeviceStatus", status, 8);
    VV_LOG_INFO("gpu: FEATURES_OK accepted, status=0x%02x (DRIVER_OK after queues, WI-1.004)", status);
    return true;
}

void VirtioGpuDevice::failDevice(const char *reason)
{
    VV_LOG_ERR("gpu: init failed: %s", reason);
    setProperty("VVInitResult", reason);
    if (mTransport != nullptr) {
        // §3.1.1: 회복할 수 없으면 FAILED를 쓴다. 그 뒤 reset해서 장치를 깨끗한 상태로 돌려 둔다.
        mTransport->addStatus(v::status::kFailed);
        mTransport->reset();
        mTransport->release();
        mTransport = nullptr;
    }
    vv::publishLog(this);
}

void VirtioGpuDevice::publishPciDiagnostics(IOPCIDevice *pci)
{
    const uint16_t command = pci->configRead16(kIOPCIConfigCommand);
    auto read8 = [pci](uint8_t offset) { return pci->configRead8(offset); };
    const uint8_t msix = vv::pci::findCapability(read8, vv::pci::kCapIdMsix);
    const uint16_t msixControl =
        msix != 0 ? pci->configRead16(static_cast<uint8_t>(msix + vv::pci::kMsixControlOffset)) : 0;
    setProperty("VVPciCommand", command, 16);
    setProperty("VVMsixControl", msixControl, 16);
    VV_LOG_INFO("gpu: pci cmd=0x%04x msix cap=0x%02x control=0x%04x", command, msix, msixControl);
}

void VirtioGpuDevice::startStatusWatch()
{
    IOWorkLoop *workLoop = getWorkLoop();
    if (workLoop == nullptr) {
        VV_LOG_ERR("gpu: no workloop for status watch");
        return;
    }
    mStatusTimer = IOTimerEventSource::timerEventSource(this, &VirtioGpuDevice::statusTimerFired);
    if (mStatusTimer == nullptr || workLoop->addEventSource(mStatusTimer) != kIOReturnSuccess) {
        VV_LOG_ERR("gpu: status watch timer setup failed");
        if (mStatusTimer != nullptr) {
            mStatusTimer->release();
            mStatusTimer = nullptr;
        }
        return;
    }
    mStatusTimer->setTimeoutMS(kStatusWatchIntervalMs);
}

void VirtioGpuDevice::stopStatusWatch()
{
    if (mStatusTimer == nullptr) {
        return;
    }
    mStatusTimer->cancelTimeout();
    if (IOWorkLoop *workLoop = getWorkLoop()) {
        workLoop->removeEventSource(mStatusTimer);
    }
    mStatusTimer->release();
    mStatusTimer = nullptr;
}

void VirtioGpuDevice::statusTimerFired(OSObject *owner, IOTimerEventSource *sender)
{
    auto *self = OSDynamicCast(VirtioGpuDevice, owner);
    if (self == nullptr) {
        return;
    }
    self->checkStatus();
    sender->setTimeoutMS(kStatusWatchIntervalMs);
}

void VirtioGpuDevice::checkStatus()
{
    if (mTransport == nullptr) {
        return;
    }
    const uint8_t status = mTransport->status();
    ++mStatusChecks;
    if (status != mExpectedStatus) {
        // 처음 바뀐 순간만 로그로 남기고, 새 값을 기준으로 다시 감시한다.
        ++mStatusMismatches;
        VV_LOG_ERR("gpu: device status changed externally 0x%02x -> 0x%02x", mExpectedStatus, status);
        mExpectedStatus = status;
        vv::publishLog(this);
    }
    setProperty("VVDeviceStatus", status, 8);
    setProperty("VVStatusChecks", mStatusChecks, 32);
    setProperty("VVStatusMismatches", mStatusMismatches, 32);
}

void VirtioGpuDevice::stop(IOService *provider)
{
    stopStatusWatch();
    if (mTransport != nullptr) {
        // 장치를 다른 드라이버(또는 다음 부팅)가 처음부터 쓸 수 있게 reset해 둔다.
        mTransport->reset();
        mTransport->release();
        mTransport = nullptr;
    }
    VV_LOG_INFO("gpu: stop");
    vv::publishLog(this);
    IOService::stop(provider);
}
