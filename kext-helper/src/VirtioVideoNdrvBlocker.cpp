// IONDRV 차단 보조 kext (WI-2.005, PHASE-PLAN-02 "DeviceProperties 방식 시험" 대안 H).
//
// 왜 필요한가:
// - 본 kext(VirtioVGA)는 IOFramebuffer를 상속하므로 IOGraphicsFamily(시스템 collection)에 링크해야 하고,
//   그래서 보조 collection으로 부팅 후반에 로드된다 (T-04). 그때는 IONDRVFramebuffer가 이미 GPU에 붙어 있다.
// - 실행 중에 IONDRV를 종료하면 AppleUpstreamUserClient가 패닉할 수 있다 (2차 시도).
// - IONDRVFramebuffer::probe는 PCI 장치에 AAPL,iokit-ignore-ndrv가 있으면 거절한다 (KDK 디스어셈블).
//   그런데 이 VM에서는 OpenCore DeviceProperties가 장치에 적용되지 않았다.
//
// 그래서 이 kext는 IOGraphicsFamily에 의존하지 않고 OpenCore로 부트 collection에 주입된다. 시스템 collection의
// IONDRV보다 먼저 GPU PCI 장치에 매칭되어, probe에서 속성만 붙이고 거절한다(장치에 붙지 않는다).
// boot-arg `-vvideooff`가 있으면 속성을 붙이지 않는다 → IONDRV가 평소대로 붙는다 (복구 경로).
#include <IOKit/IOService.h>

#include "VVBootArgs.hpp"

class VirtioVideoNdrvBlocker : public IOService {
    OSDeclareDefaultStructors(VirtioVideoNdrvBlocker)

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
};

OSDefineMetaClassAndStructors(VirtioVideoNdrvBlocker, IOService)

IOService *VirtioVideoNdrvBlocker::probe(IOService *provider, SInt32 *)
{
    // 붙지 않으므로 로그를 남길 객체가 없다. 결과는 PCI 장치의 VVNdrvBlocked 속성으로 확인한다.
    if (provider == nullptr) {
        return nullptr;
    }
    if (vv::isDriverDisabledByBootArg()) {
        provider->setProperty("VVNdrvBlocked", "skipped:-vvideooff");
        return nullptr;
    }
    if (provider->getProperty(vv::kNdrvHandedBackProperty) != nullptr) {
        // 본 kext가 실패해 IONDRV에 화면을 돌려준 뒤의 재매칭이다. 다시 막지 않는다.
        return nullptr;
    }
    provider->setProperty(vv::kIgnoreNdrvProperty, kOSBooleanTrue);
    provider->setProperty("VVNdrvBlocked", "set");
    return nullptr;
}
