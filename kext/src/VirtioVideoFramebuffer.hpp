// IOFramebuffer 하위 클래스 (WI-2.005, REQ-001, REQ-031, REQ-051, REQ-052).
//
// 같은 PCI 장치에 붙어 있는 VirtioGpuDevice(별도 카테고리)를 찾아 그 프레임버퍼 메모리를 aperture로 쓴다.
// 화면 출력(scanout·주기 전송)은 VirtioGpuDevice가 맡는다. 이 클래스는 IOGraphics가 요구하는 모드·픽셀 정보를
// 보고하고 enableController에서 출력을 시작한다.
//
// 이 단계의 범위: 장치 선호 해상도 한 모드(기본 1280×800), 32bpp, 소프트웨어 커서, 감마는 받기만 한다.
// 모드 목록·전환·EDID·VRAM 128 MiB 보고는 P3에서 한다.
//
// 매칭: IOPCIDevice, IOFramebuffer 카테고리, 점수 30000(IONDRVFramebuffer 20000보다 높게). boot-arg
// `vvideo_takeover=1`이 없으면 probe에서 빠진다. IONDRV가 이미 붙어 있으면 점수만으로는 대체되지 않으므로
// VirtioGpuDevice가 IONDRV를 내리고 매칭을 다시 돌린다 (VirtioGpuDeviceTakeover.cpp).
#pragma once

#include <IOKit/graphics/IOFramebuffer.h>

#include "vv/DisplayMode.hpp"
#include "vv/ModeTable.hpp"

class VirtioGpuDevice;
class VVDmaBuffer;

class VirtioVideoFramebuffer : public IOFramebuffer {
    OSDeclareDefaultStructors(VirtioVideoFramebuffer)

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;

    IOReturn enableController() override;
    bool isConsoleDevice() override;
    IODeviceMemory *getApertureRange(IOPixelAperture aperture) override;
    IODeviceMemory *getVRAMRange() override;
    const char *getPixelFormats() override;
    IOItemCount getDisplayModeCount() override;
    IOReturn getDisplayModes(IODisplayModeID *allDisplayModes) override;
    IOReturn getInformationForDisplayMode(IODisplayModeID displayMode,
                                          IODisplayModeInformation *info) override;
    UInt64 getPixelFormatsForDisplayMode(IODisplayModeID displayMode, IOIndex depth) override;
    IOReturn getPixelInformation(IODisplayModeID displayMode, IOIndex depth,
                                 IOPixelAperture aperture, IOPixelInformation *pixelInfo) override;
    IOReturn getCurrentDisplayMode(IODisplayModeID *displayMode, IOIndex *depth) override;
    IOReturn setDisplayMode(IODisplayModeID displayMode, IOIndex depth) override;
    IOReturn getStartupDisplayMode(IODisplayModeID *displayMode, IOIndex *depth) override;
    IOReturn setGammaTable(UInt32 channelCount, UInt32 dataCount, UInt32 dataWidth, void *data) override;
    IOReturn getAttribute(IOSelect attribute, uintptr_t *value) override;
    IOReturn setAttribute(IOSelect attribute, uintptr_t value) override;
    IOReturn getAttributeForConnection(IOIndex connectIndex, IOSelect attribute, uintptr_t *value) override;
    // EDID (WI-3.003, REQ-025). GPU 서비스가 검증한 EDID를 128바이트 블록으로 준다.
    bool hasDDCConnect(IOIndex connectIndex) override;
    IOReturn getDDCBlock(IOIndex connectIndex, UInt32 blockNumber, IOSelect blockType, IOOptionBits options,
                         UInt8 *data, IOByteCount *length) override;

    // 디스플레이 변경 이벤트 뒤 GPU 서비스가 부른다 (WI-3.005). 선호 해상도가 바뀌었으면 모드 목록을 다시 만든다.
    // IOGraphics에 연결 변경을 알려 즉시 다시 읽게 하는 것은 아직 하지 않는다 (창 크기 자동 전환은 WI-6.001).
    void displayConfigChanged();

private:
    static VirtioGpuDevice *findGpuDevice(IOService *pci);
    IODeviceMemory *makeApertureRange() const;
    // IOGraphics가 어떤 순서로 무엇을 부르는지 VVFbTrace 속성에 남긴다 (2차 시도에서 enableController가 불리지
    // 않은 원인 확인용, PHASE-PLAN-02). 처음 kMaxTraceEntries개만 남긴다.
    void trace(const char *what, uint32_t selector = 0, uint64_t value = 0);

    // 전원 관리 트리에 들어간다 (WI-4.002). IOFramebuffer 기본 클래스는 PMinit·registerPowerDriver를 하지 않으며,
    // IONDRVFramebuffer도 자기 initForPM()에서 직접 한다 (KDK 심볼 확인). 하지 않으면 디스플레이 잠자기 때
    // setAttribute(kIOPowerAttribute)가 오지 않는다 (0.0.30 실측).
    void initForPM();
    bool mPMInitialized = false;
    uintptr_t mPowerState = 0;  // IOGraphics가 setAttribute(kIOPowerAttribute)로 준 마지막 값
    uint32_t mTraceEntries = 0;

    VirtioGpuDevice *mGpu = nullptr;       // retain
    VVDmaBuffer *mBuffer = nullptr;        // retain: GPU 서비스가 먼저 멈춰도 aperture가 해제된 메모리를 가리키지 않게
    vv::display::Mode mMode{};             // 현재 모드
    vv::display::ModeTable mModes{};       // 보고하는 모드 목록 (WI-3.001)
    IODisplayModeID mCurrentModeId = 0;
    IODisplayModeID mStartupModeId = 0;    // 장치 선호 해상도의 모드
    bool mScanoutStarted = false;
};
