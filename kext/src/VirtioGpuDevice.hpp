// virtio-gpu 장치 서비스 (P1). PCI 장치에 별도 IOMatchCategory로 붙는다 (AppleVirtIOPCITransport와 공존,
// docs/research/apple-virtio-transport.md). 전송 계층을 소유하고 장치 초기화를 진행한다.
//
// 초기화 순서 (§3.1.1): reset → ACKNOWLEDGE → DRIVER → feature 협상 → FEATURES_OK 확인(WI-1.003)
// → 인터럽트 준비(WI-1.005) → controlq·cursorq 등록(WI-1.004) → DRIVER_OK → GET_DISPLAY_INFO 1회(WI-1.007).
//
// 명령 (WI-1.007, REQ-023, REQ-024): controlq에 한 번에 하나만 보낸다. IOCommandGate 안에서 요청을 링에 올리고
// notify한 뒤 commandSleep으로 완료를 기다린다(타임아웃 상수). 완료는 인터럽트·Poll 처리기가 같은 워크루프에서
// used 링을 회수하며 commandWakeup으로 알린다. 응답은 길이·type을 검사한 뒤에만 쓴다.
//
// 인터럽트 (WI-1.005, boot-arg `vvideo_irq=0|1|2`):
//   IOPCIDevice가 이미 붙여 둔 인터럽트 소스를 그대로 쓴다. configureInterrupts는 이 장치에서
//   kIOReturnUnsupported를 돌려준다(WI-1.005 실측) — PCI 계층이 MSI-X 소스 1개를 미리 구성해 둔다.
//   MSI-X(기본): 소스가 3개 이상이면 벡터 0 config, 1 controlq, 2 cursorq. 1~2개면 셋 다 벡터 0을 같이 쓰고,
//     인터럽트마다 config 이벤트와 두 큐를 모두 확인한다.
//   INTx: 레거시(레벨) 소스가 있을 때만. 필터에서 ISR을 읽어(읽으면 지워짐) 원인을 저장하고 워크루프에서 처리한다.
//   Poll: 디버그용이자 마지막 대체 수단. 타이머로 used 링과 config 이벤트를 확인한다.
//   요청한 방식의 소스가 없으면 MSI-X → INTx → Poll 순으로 내려간다.
// 모든 처리는 이 서비스의 워크루프에서 직렬화된다.
//
// 주기 타이머로 장치 상태를 감시한다 (T-05). DEVICE_NEEDS_RESET이 오르거나 DRIVER_OK가 사라지거나 명령이
// 시간 초과되면 reset 후 처음부터 다시 올린다 (WI-1.006, REQ-017). 복구는 시작 한 번에 kMaxRecoveries회까지만 하고,
// 넘으면 장치를 FAILED로 두고 손을 뗀다. 복구 뒤 GET_DISPLAY_INFO를 워크루프 밖(thread_call)에서 다시 보내
// 명령 경로까지 살아났는지 확인한다. boot-arg `vvideo_test_reset=1`이면 시작 30초 뒤 외부 reset을 흉내 내 복구 경로를 시험한다.
// P1 동안은 디버깅을 위해 초기화에 실패해도 start()가 true를 돌려 객체를 남기고 결과를 VV* 속성으로 공개한다.
// P2에서 프레임버퍼와 연결할 때는 실패 시 false를 돌려준다 (REQ-005).
#pragma once

#include <IOKit/IOService.h>

#include <kern/thread_call.h>

#include "vv/DeviceHealth.hpp"
#include "vv/VirtioIrq.hpp"

class IOCommandGate;
class IOFilterInterruptEventSource;
class IOInterruptEventSource;
class IOPCIDevice;
class IOTimerEventSource;
class IOWorkLoop;
class VirtioPciTransport;
class VirtioQueue;
class VVDmaBuffer;

class VirtioGpuDevice : public IOService {
    OSDeclareDefaultStructors(VirtioGpuDevice)

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOWorkLoop *getWorkLoop() const override;

private:
    static constexpr unsigned kQueueCount = 2;

    bool bringUpDevice(const char **failureReason);
    void recoverDevice(const char *reason);
    void giveUpDevice(const char *reason);
    void releaseQueues();
    static void deferredDisplayQuery(thread_call_param_t owner, thread_call_param_t);
    static void testResetFired(OSObject *owner, IOTimerEventSource *sender);
    bool prepareInterrupts();
    bool prepareMsix();
    bool prepareIntx();
    void findInterruptSources();
    bool setupQueues();
    void enableInterrupts();
    void releaseInterrupts();
    void releaseDevice();
    void failDevice(const char *reason);
    void publishPciDiagnostics(IOPCIDevice *pci);
    void publishInterruptSources();
    void startTimers();
    void stopTimers();
    void checkStatus();
    void publishCounters();

    // 명령 하나를 보내고 완료를 기다린다. 응답을 response에 복사하고 장치가 쓴 길이를 written에 남긴다.
    IOReturn submitCommand(const void *request, uint32_t requestBytes, void *response, uint32_t responseBytes,
                           uint32_t *written);
    static IOReturn gatedSubmit(OSObject *owner, void *request, void *response, void *lengths, void *written);
    void queryDisplayInfo();

    void handleQueueEvent(unsigned queue);
    void handleConfigEvent();
    void handleSharedEvent();

    static void msixFired(OSObject *owner, IOInterruptEventSource *sender, int count);
    static bool intxFilter(OSObject *owner, IOFilterInterruptEventSource *sender);
    static void intxFired(OSObject *owner, IOInterruptEventSource *sender, int count);
    static void statusTimerFired(OSObject *owner, IOTimerEventSource *sender);
    static void pollTimerFired(OSObject *owner, IOTimerEventSource *sender);

    IOPCIDevice *mPci = nullptr;
    IOWorkLoop *mWorkLoop = nullptr;
    VirtioPciTransport *mTransport = nullptr;
    VirtioQueue *mQueues[kQueueCount] = {};

    vv::virtio::IrqMode mIrqMode = vv::virtio::kDefaultIrqMode;
    static constexpr int kNoSource = -1;
    int mMsixIndices[vv::virtio::kSeparateMsixVectors] = {kNoSource, kNoSource, kNoSource};
    uint16_t mMsixSourceCount = 0;
    int mLegacyIndex = kNoSource;
    IOInterruptEventSource *mMsixSources[vv::virtio::kSeparateMsixVectors] = {};
    IOFilterInterruptEventSource *mIntxSource = nullptr;
    volatile uint32_t mIntxPendingIsr = 0;  // 필터(1차 인터럽트 문맥)가 쌓고 워크루프가 비운다

    IOCommandGate *mGate = nullptr;
    VVDmaBuffer *mCommandBuffer = nullptr;  // 요청·응답을 담는 장치 공유 메모리
    bool mCommandInFlight = false;
    bool mCommandDone = false;
    uint16_t mCommandHead = 0;
    uint32_t mCommandWritten = 0;
    uint32_t mCommandsSent = 0;
    uint32_t mCommandTimeouts = 0;

    IOTimerEventSource *mStatusTimer = nullptr;
    IOTimerEventSource *mPollTimer = nullptr;
    IOTimerEventSource *mTestResetTimer = nullptr;
    thread_call_t mDisplayQueryCall = nullptr;
    volatile SInt32 mDisplayQueryActive = 0;  // 실행 중인 deferredDisplayQuery 수. stop에서 0이 될 때까지 기다린다
    bool mStopping = false;
    uint32_t mRecoveries = 0;
    bool mGivenUp = false;
    uint8_t mExpectedStatus = 0;
    uint32_t mStatusChecks = 0;
    uint32_t mStatusMismatches = 0;

    uint32_t mInterrupts = 0;
    uint32_t mConfigEvents = 0;
    uint32_t mQueueEvents[kQueueCount] = {};
    uint32_t mCompletions[kQueueCount] = {};
    uint32_t mDeviceErrors = 0;
};
