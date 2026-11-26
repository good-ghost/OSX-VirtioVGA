// 커널 로그 매크로와 로그 수준 (WI-0.005, REQ-064).
//
// 수준은 boot-arg `vvideo_log=<0|1|2>`로 정한다. 0=오류, 1=정보(기본), 2=디버그.
// 왜 boot-arg인가: 드라이버를 다시 빌드·주입하지 않고 OpenCore boot-args만 바꿔 조절하기 위해서다.
//
// 출력 경로 (WI-0.005 실측, PHASE-PLAN-00 결정 기록):
//   OpenCore로 주입한 kext의 IOLog/os_log 메시지는 통합 로그(`/usr/bin/log show`)와 `dmesg` 어디에도
//   나타나지 않았다. 그래서 메시지를 드라이버 안의 고정 크기 링 버퍼에 모으고, 안전한 시점에
//   IORegistry 속성 `VVLog`로 공개한다. 읽기: `ioreg -r -c <클래스> -l -w0 | grep VVLog`.
//   os_log에도 함께 보낸다 (kext가 디스크의 kernel collection에 들어가는 경우를 대비).
//
// 제약: 인터럽트(필터) 컨텍스트에서 호출하지 않는다. 버퍼 기록은 IOSimpleLock으로 보호한다.
#pragma once

#include <IOKit/IOService.h>
#include <os/log.h>

#include "vv/LogLevel.hpp"

namespace vv {

// 링 버퍼 크기. 메시지 하나는 잘려서 저장된다.
inline constexpr unsigned kLogRingEntries = 128;
inline constexpr unsigned kLogLineBytes = 160;

// IORegistry에 공개할 속성 이름.
inline constexpr const char *kLogProperty = "VVLog";
inline constexpr const char *kLogLevelProperty = "VVLogLevel";
inline constexpr const char *kLogDroppedProperty = "VVLogDropped";

// 드라이버 start() 초반에 한 번 호출한다. 실패하면 false (메모리 부족).
bool initLog();
// 드라이버 stop()/free()에서 호출한다.
void shutdownLog();

LogLevel currentLogLevel();

inline bool isLogEnabled(LogLevel level)
{
    return static_cast<int>(level) <= static_cast<int>(currentLogLevel());
}

// 버퍼에 한 줄 기록한다. 매크로를 통해 호출한다.
void logLine(LogLevel level, const char *format, ...) __attribute__((format(printf, 2, 3)));

// 버퍼 내용을 service의 IORegistry 속성으로 공개한다. 인터럽트 컨텍스트가 아닌 곳에서만 호출한다.
void publishLog(IOService *service);

} // namespace vv

#define VV_LOG_PREFIX "VirtioVGA: "

#define VV_LOG_AT(level, osLogType, fmt, ...)                                         \
    do {                                                                              \
        if (vv::isLogEnabled(level)) {                                                \
            os_log_with_type(OS_LOG_DEFAULT, osLogType, VV_LOG_PREFIX fmt, ##__VA_ARGS__); \
            vv::logLine(level, fmt, ##__VA_ARGS__);                                   \
        }                                                                             \
    } while (0)

#define VV_LOG_ERR(fmt, ...)  VV_LOG_AT(vv::LogLevel::Error, OS_LOG_TYPE_ERROR, fmt, ##__VA_ARGS__)
#define VV_LOG_INFO(fmt, ...) VV_LOG_AT(vv::LogLevel::Info, OS_LOG_TYPE_DEFAULT, fmt, ##__VA_ARGS__)
#define VV_LOG_DBG(fmt, ...)  VV_LOG_AT(vv::LogLevel::Debug, OS_LOG_TYPE_DEFAULT, fmt, ##__VA_ARGS__)
