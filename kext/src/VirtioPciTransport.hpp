// virtio 1.x modern PCI 전송 계층 (REQ-010, REQ-018).
// WI-1.002: capability를 찾아 영역을 고르고 BAR를 매핑한 뒤, 영역 경계를 검사하는 레지스터 접근을 제공한다.
// WI-1.003: reset, 상태 비트, feature 읽기·쓰기 (§3.1.1 초기화 순서의 앞부분).
// WI-1.004: 큐 등록 (§4.1.5.1.3).
// WI-1.005: MSI-X 벡터 배정(다시 읽어 장치가 받아들였는지 확인), ISR 읽기, device config 쓰기.
// WI-1.007: 큐 알림(notify).
// GPU 코드와 분리해 다른 virtio 장치에도 쓸 수 있게 둔다.
#pragma once

#include <IOKit/IOService.h>

#include "vv/VirtioPciLayout.hpp"

class IOPCIDevice;
class VirtioQueue;
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
    bool deviceWrite32(uint32_t offset, uint32_t value);
    // §4.1.4.5: ISR 상태를 읽는다. 읽으면 장치가 0으로 지운다 (INTx 방식에서만 쓴다).
    uint8_t readIsr();
    // §4.1.5.1.2: config 변경 벡터를 쓰고 다시 읽는다. 장치가 거절하면 NO_VECTOR가 읽히고 false.
    bool setConfigVector(uint16_t vector);

    bool commonWrite8(uint32_t offset, uint8_t value);
    bool commonWrite16(uint32_t offset, uint16_t value);
    bool commonWrite32(uint32_t offset, uint32_t value);
    bool commonWrite64(uint32_t offset, uint64_t value);  // 하위·상위 32비트로 나눠 쓴다 (§4.1.3.1 허용)

    // §4.1.4.3.2: device_status에 0을 쓰고 0이 읽힐 때까지 기다린다. 시간 안에 안 되면 false.
    bool reset();
    uint8_t status() const;
    // 현재 상태에 비트를 더해 쓴다 (§3.1.1의 단계별 비트 설정).
    bool addStatus(uint8_t bits);
    // 64비트 장치 feature를 읽는다 (select 0, 1).
    uint64_t readDeviceFeatures();
    bool writeDriverFeatures(uint64_t features);
    uint64_t readDriverFeatures();

    uint16_t numQueues() const;
    // 장치가 허용하는 최대 큐 크기. 0이면 그 큐는 없다. queue_select에 쓴다.
    uint16_t queueMaxSize(uint16_t index);
    // 큐 메모리 주소·크기·MSI-X 벡터를 장치에 알리고 켠다. 알림 오프셋을 queue에 기록한다.
    // vector가 NO_VECTOR가 아니면 다시 읽어 장치가 받아들였는지 확인한다.
    bool setupQueue(VirtioQueue *queue, uint16_t vector);
    // 큐가 켜져 있는지 확인한다. 끄는 것은 reset으로만 한다 (드라이버가 queue_enable에 0을 쓰는 것은 사양상 금지).
    bool isQueueEnabled(uint16_t index);
    // §4.1.4.4: 큐의 notify 주소에 큐 번호(16비트)를 써서 장치에 새 버퍼를 알린다.
    // 링 갱신이 먼저 보이도록 쓰기 전에 메모리 배리어를 둔다 (§2.7.13).
    bool notifyQueue(VirtioQueue *queue);

    void free() override;

private:
    bool initWithDevice(IOPCIDevice *pci, uint32_t minDeviceCfgBytes, vv::virtio::pci::LayoutError *errorOut);
    bool mapRegion(const vv::virtio::pci::Region &region);
    volatile uint8_t *regionAddress(const vv::virtio::pci::Region &region, uint32_t offset, uint32_t width) const;

    IOPCIDevice *mPci = nullptr;
    IOMemoryMap *mBarMaps[vv::virtio::pci::kBarCount] = {};
    vv::virtio::pci::Layout mLayout;
};
