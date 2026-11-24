// IOFramebuffer 하위 클래스.
// 현재는 T-04 확인용 스텁이다 (PHASE-PLAN-02 "T-04 재조사"): IOGraphicsFamily 심볼(vtable·metaclass)을 쓰는
// kext가 OpenCore 주입(부트 kernel collection)으로 로드되는지만 본다.
// Info.plist에 personality가 없으므로 어떤 장치에도 붙지 않는다. 실제 구현은 WI-2.005에서 채운다.
#pragma once

#include <IOKit/graphics/IOFramebuffer.h>

class VirtioVideoFramebuffer : public IOFramebuffer {
    OSDeclareDefaultStructors(VirtioVideoFramebuffer)

public:
    IODeviceMemory *getApertureRange(IOPixelAperture aperture) override;
    const char *getPixelFormats() override;
    IOItemCount getDisplayModeCount() override;
    IOReturn getDisplayModes(IODisplayModeID *allDisplayModes) override;
    IOReturn getInformationForDisplayMode(IODisplayModeID displayMode,
                                          IODisplayModeInformation *info) override;
    UInt64 getPixelFormatsForDisplayMode(IODisplayModeID displayMode, IOIndex depth) override;
    IOReturn getPixelInformation(IODisplayModeID displayMode, IOIndex depth,
                                 IOPixelAperture aperture, IOPixelInformation *pixelInfo) override;
    IOReturn getCurrentDisplayMode(IODisplayModeID *displayMode, IOIndex *depth) override;
};
