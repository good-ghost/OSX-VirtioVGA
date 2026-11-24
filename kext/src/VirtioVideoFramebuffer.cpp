#include "VirtioVideoFramebuffer.hpp"

// T-04 확인용 스텁. 장치에 붙지 않으므로 아래 메서드는 호출되지 않는다.
// 호출되더라도 "지원하지 않음"을 돌려주어 IOGraphics가 이 객체를 쓰지 않게 한다.

OSDefineMetaClassAndStructors(VirtioVideoFramebuffer, IOFramebuffer)

IODeviceMemory *VirtioVideoFramebuffer::getApertureRange(IOPixelAperture)
{
    return nullptr;
}

const char *VirtioVideoFramebuffer::getPixelFormats()
{
    return "";
}

IOItemCount VirtioVideoFramebuffer::getDisplayModeCount()
{
    return 0;
}

IOReturn VirtioVideoFramebuffer::getDisplayModes(IODisplayModeID *)
{
    return kIOReturnUnsupported;
}

IOReturn VirtioVideoFramebuffer::getInformationForDisplayMode(IODisplayModeID, IODisplayModeInformation *)
{
    return kIOReturnUnsupported;
}

UInt64 VirtioVideoFramebuffer::getPixelFormatsForDisplayMode(IODisplayModeID, IOIndex)
{
    // 헤더 설명: 하위 클래스는 0을 돌려줘야 한다 (obsolete).
    return 0;
}

IOReturn VirtioVideoFramebuffer::getPixelInformation(IODisplayModeID, IOIndex, IOPixelAperture,
                                                     IOPixelInformation *)
{
    return kIOReturnUnsupported;
}

IOReturn VirtioVideoFramebuffer::getCurrentDisplayMode(IODisplayModeID *, IOIndex *)
{
    return kIOReturnUnsupported;
}
