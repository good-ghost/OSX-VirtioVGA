#include "VirtioVideoProbe.hpp"

#include "VVBootArgs.hpp"
#include "VVLog.hpp"

OSDefineMetaClassAndStructors(VirtioVideoProbe, IOService)

namespace {
// 로그와 별개로 start() 호출 여부를 ioreg로 확인하기 위한 속성.
constexpr const char *kStartedProperty = "VVProbeStarted";
} // namespace

IOService *VirtioVideoProbe::probe(IOService *provider, SInt32 *score)
{
    // 거절하면 객체가 바로 해제되므로 로그를 남길 곳이 없다. 확인은 ioreg에 객체가 없는 것으로 한다.
    if (vv::isDriverDisabledByBootArg()) {
        return nullptr;
    }
    return IOService::probe(provider, score);
}

bool VirtioVideoProbe::start(IOService *provider)
{
    if (!IOService::start(provider)) {
        return false;
    }
    if (!vv::initLog()) {
        // 로그 없이도 동작은 계속한다. 실패 사실은 속성으로 남긴다.
        setProperty("VVLogInitFailed", true);
    }
    setProperty(kStartedProperty, true);

    VV_LOG_ERR("probe start (error level) loglevel=%d", static_cast<int>(vv::currentLogLevel()));
    VV_LOG_INFO("probe start (info level)");
    VV_LOG_DBG("probe start (debug level)");
    vv::publishLog(this);
    return true;
}

void VirtioVideoProbe::stop(IOService *provider)
{
    VV_LOG_INFO("probe stop");
    vv::publishLog(this);
    vv::shutdownLog();
    IOService::stop(provider);
}
