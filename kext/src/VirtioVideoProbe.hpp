// P0 확인용 최소 서비스 클래스. IOResources에 매칭해 로드·로그·boot-arg 동작을 확인한다.
// P2에서 실제 프레임버퍼 클래스가 생기면 이 클래스는 제거한다.
#pragma once

#include <IOKit/IOService.h>

class VirtioVideoProbe : public IOService {
    OSDeclareDefaultStructors(VirtioVideoProbe)

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
};
