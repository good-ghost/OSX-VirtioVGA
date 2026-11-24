// virtio 1.x modern PCI 전송 계층 (REQ-010, REQ-018).
// WI-1.002: capability를 찾아 영역을 고르고 BAR를 매핑한 뒤, 영역 경계를 검사하는 레지스터 접근을 제공한다.
// WI-1.003: reset, 상태 비트, feature 읽기·쓰기 (§3.1.1 초기화 순서의 앞부분). 큐 설정은 WI-1.004.
// GPU 코드와 분리해 다른 virtio 장치에도 쓸 수 있게 둔다.
#pragma once

#include <IOKit/IOService.h>

#include "vv/VirtioPciLayout.hpp"

class IOPCIDevice;
class IOMemoryMap;

class VirtioPciTransport : public OSObject {
    OSDeclareDefaultStructors(VirtioPciTransport)

public:
    // minDeviceCfgBytes: 장치 종류별 config 구조체 크기. 실패하면 nullptr이고, errorOut이 있으면 영역 선택 결과를 남긴다.
    static VirtioPciTransport *withDevice(IOPCIDevice *pci, uint32_t minDeviceCfgBytes,
                                          vv::virtio::pci::LayoutError *errorOut);

    const vv::virtio::pci::Layout &layout() const { return mLayout; }

    // 영역 안의 오프셋만 허용한다. 범위를 벗어나면 0을 돌려주고 false를 out에 남긴다.
    uint8_t commonRead8(uint32_t offset, bool *ok = nullptr) const;
    uint16_t commonRead16(uint32_t offset, bool *ok = nullptr) const;
    uint32_t commonRead32(uint32_t offset, bool *ok = nullptr) const;
    uint32_t deviceRead32(uint32_t offset, bool *ok = nullptr) const;

    bool commonWrite8(uint32_t offset, uint8_t value);
    bool commonWrite32(uint32_t offset, uint32_t value);

    // §4.1.4.3.2: device_status에 0을 쓰고 0이 읽힐 때까지 기다린다. 시간 안에 안 되면 false.
    bool reset();
    uint8_t status() const;
    // 현재 상태에 비트를 더해 쓴다 (§3.1.1의 단계별 비트 설정).
    bool addStatus(uint8_t bits);
    // 64비트 장치 feature를 읽는다 (select 0, 1).
    uint64_t readDeviceFeatures();
    bool writeDriverFeatures(uint64_t features);
    uint64_t readDriverFeatures();

    void free() override;

private:
    bool initWithDevice(IOPCIDevice *pci, uint32_t minDeviceCfgBytes, vv::virtio::pci::LayoutError *errorOut);
    bool mapRegion(const vv::virtio::pci::Region &region);
    volatile uint8_t *regionAddress(const vv::virtio::pci::Region &region, uint32_t offset, uint32_t width) const;

    IOPCIDevice *mPci = nullptr;
    IOMemoryMap *mBarMaps[vv::virtio::pci::kBarCount] = {};
    vv::virtio::pci::Layout mLayout;
};
