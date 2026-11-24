// WI-1.001 조사용 진단 서비스. virtio-gpu PCI 장치(1af4:1050)에 별도 IOMatchCategory로 붙어,
// 장치 상태를 **읽기만** 하고 IORegistry 속성(VV*)으로 공개한다. 장치 레지스터에 아무것도 쓰지 않는다.
// 목적: 같은 장치에 붙는 AppleVirtIOPCITransport가 장치를 초기화했는지(device_status, feature, MSI-X)를 확인한다.
// P1 본 구현(VirtioPciTransport)이 생기면 이 클래스는 제거한다.
#pragma once

#include <IOKit/IOService.h>

class IOPCIDevice;

class VirtioVideoPciProbe : public IOService {
    OSDeclareDefaultStructors(VirtioVideoPciProbe)

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

private:
    void publishPciHeader(IOPCIDevice *pci);
    void publishMsix(IOPCIDevice *pci);
    void publishVirtioCaps(IOPCIDevice *pci);
};
