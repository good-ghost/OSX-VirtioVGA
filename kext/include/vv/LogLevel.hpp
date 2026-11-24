// 로그 수준 정의와 boot-arg 해석 (WI-0.005).
// 커널 헤더에 의존하지 않아 사용자 공간 단위 테스트(tests/)에서도 컴파일된다.
#pragma once

namespace vv {

enum class LogLevel : int {
    Error = 0,
    Info = 1,
    Debug = 2,
};

inline constexpr const char *kLogLevelBootArg = "vvideo_log";
inline constexpr LogLevel kDefaultLogLevel = LogLevel::Info;

// boot-arg에서 읽은 값(없으면 hasValue=false)을 수준으로 바꾼다. 범위 밖이면 기본값.
constexpr LogLevel logLevelFromBootArg(bool hasValue, int value)
{
    if (!hasValue) {
        return kDefaultLogLevel;
    }
    if (value < static_cast<int>(LogLevel::Error) || value > static_cast<int>(LogLevel::Debug)) {
        return kDefaultLogLevel;
    }
    return static_cast<LogLevel>(value);
}

} // namespace vv
