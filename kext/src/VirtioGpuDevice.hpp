// virtio-gpu 장치 서비스 (P1). PCI 장치에 별도 IOMatchCategory로 붙는다 (AppleVirtIOPCITransport와 공존,
// docs/research/apple-virtio-transport.md). 전송 계층을 소유하고 장치 초기화를 진행한다.
//
// 초기화 순서 (§3.1.1): reset → ACKNOWLEDGE → DRIVER → feature 협상 → FEATURES_OK 확인(WI-1.003)
// → controlq·cursorq 등록 → DRIVER_OK (WI-1.004). 아직 명령은 보내지 않는다 (WI-1.007).
// 주기 타이머로 장치 상태가 외부에서 바뀌지 않는지 감시한다 (T-05).
//
// P1 동안은 디버깅을 위해 초기화에 실패해도 start()가 true를 돌려 객체를 남기고 결과를 VV* 속성으로 공개한다.
// P2에서 프레임버퍼와 연결할 때는 실패 시 false를 돌려준다 (REQ-005).
#pragma once

#include <IOKit/IOService.h>

class IOPCIDevice;
class IOTimerEventSource;
class VirtioPciTransport;
class VirtioQueue;

class VirtioGpuDevice : public IOService {
    OSDeclareDefaultStructors(VirtioGpuDevice)

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

private:
    bool initializeDevice();
    bool setupQueues();
    void releaseDevice();
    void failDevice(const char *reason);
    void publishPciDiagnostics(IOPCIDevice *pci);
    void startStatusWatch();
    void stopStatusWatch();
    void checkStatus();
    static void statusTimerFired(OSObject *owner, IOTimerEventSource *sender);

    VirtioPciTransport *mTransport = nullptr;
    VirtioQueue *mControlQueue = nullptr;
    VirtioQueue *mCursorQueue = nullptr;
    IOTimerEventSource *mStatusTimer = nullptr;
    uint8_t mExpectedStatus = 0;
    uint32_t mStatusChecks = 0;
    uint32_t mStatusMismatches = 0;
};
