#include "TestMain.hpp"

#include "vv/LogLevel.hpp"

using vv::LogLevel;
using vv::logLevelFromBootArg;

VV_TEST(logLevel_missingBootArg_usesDefault)
{
    VV_EXPECT(logLevelFromBootArg(false, 0) == vv::kDefaultLogLevel);
    VV_EXPECT(logLevelFromBootArg(false, 2) == vv::kDefaultLogLevel);
}

VV_TEST(logLevel_validValues_mapDirectly)
{
    VV_EXPECT(logLevelFromBootArg(true, 0) == LogLevel::Error);
    VV_EXPECT(logLevelFromBootArg(true, 1) == LogLevel::Info);
    VV_EXPECT(logLevelFromBootArg(true, 2) == LogLevel::Debug);
}

VV_TEST(logLevel_outOfRange_usesDefault)
{
    VV_EXPECT(logLevelFromBootArg(true, -1) == vv::kDefaultLogLevel);
    VV_EXPECT(logLevelFromBootArg(true, 3) == vv::kDefaultLogLevel);
    VV_EXPECT(logLevelFromBootArg(true, 1000) == vv::kDefaultLogLevel);
}
