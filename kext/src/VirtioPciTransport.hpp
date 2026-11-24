// virtio 1.x modern PCI 전송 계층 (REQ-010, REQ-018).
// WI-1.002: capability를 찾아 영역을 고르고 BAR를 매핑한 뒤, 영역 경계를 검사하는 레지스터 접근을 제공한다.
// 장치 초기화(reset·feature·큐)는 WI-1.003 이후에 이 클래스에 더한다.
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

    void free() override;

private:
    bool initWithDevice(IOPCIDevice *pci, uint32_t minDeviceCfgBytes, vv::virtio::pci::LayoutError *errorOut);
    bool mapRegion(const vv::virtio::pci::Region &region);
    volatile uint8_t *regionAddress(const vv::virtio::pci::Region &region, uint32_t offset, uint32_t width) const;

    IOPCIDevice *mPci = nullptr;
    IOMemoryMap *mBarMaps[vv::virtio::pci::kBarCount] = {};
    vv::virtio::pci::Layout mLayout;
};
