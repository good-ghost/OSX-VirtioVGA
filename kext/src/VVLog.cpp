#include "VVLog.hpp"

#include <IOKit/IOLib.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSString.h>
#include <pexpert/pexpert.h>
#include <stdarg.h>

namespace vv {

namespace {

struct LogRing {
    char lines[kLogRingEntries][kLogLineBytes];
    unsigned long long nextSequence; // 지금까지 기록한 줄 수. 가장 오래된 줄 위치 계산에 쓴다.
};

LogLevel gLogLevel = kDefaultLogLevel;
LogRing *gRing = nullptr;
IOSimpleLock *gLock = nullptr;

constexpr char levelTag(LogLevel level)
{
    switch (level) {
    case LogLevel::Error: return 'E';
    case LogLevel::Info:  return 'I';
    case LogLevel::Debug: return 'D';
    }
    return '?';
}

} // namespace

bool initLog()
{
    int value = 0;
    const bool hasValue = PE_parse_boot_argn(kLogLevelBootArg, &value, sizeof(value));
    gLogLevel = logLevelFromBootArg(hasValue, value);

    if (gRing != nullptr) {
        return true;
    }
    gLock = IOSimpleLockAlloc();
    if (gLock == nullptr) {
        return false;
    }
    gRing = static_cast<LogRing *>(IOMallocZero(sizeof(LogRing)));
    if (gRing == nullptr) {
        IOSimpleLockFree(gLock);
        gLock = nullptr;
        return false;
    }
    return true;
}

void shutdownLog()
{
    if (gRing != nullptr) {
        IOFree(gRing, sizeof(LogRing));
        gRing = nullptr;
    }
    if (gLock != nullptr) {
        IOSimpleLockFree(gLock);
        gLock = nullptr;
    }
}

LogLevel currentLogLevel()
{
    return gLogLevel;
}

void logLine(LogLevel level, const char *format, ...)
{
    if (gRing == nullptr || gLock == nullptr) {
        return;
    }
    char message[kLogLineBytes];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    IOSimpleLockLock(gLock);
    const unsigned long long sequence = gRing->nextSequence++;
    char *slot = gRing->lines[sequence % kLogRingEntries];
    snprintf(slot, kLogLineBytes, "%llu %c %s", sequence, levelTag(level), message);
    IOSimpleLockUnlock(gLock);
}

void publishLog(IOService *service)
{
    if (service == nullptr || gRing == nullptr || gLock == nullptr) {
        return;
    }
    // 락을 오래 잡지 않도록 먼저 복사한 뒤 OSString을 만든다.
    LogRing *snapshot = static_cast<LogRing *>(IOMalloc(sizeof(LogRing)));
    if (snapshot == nullptr) {
        return;
    }
    IOSimpleLockLock(gLock);
    bcopy(gRing, snapshot, sizeof(LogRing));
    IOSimpleLockUnlock(gLock);

    const unsigned long long total = snapshot->nextSequence;
    const unsigned long long kept = total < kLogRingEntries ? total : kLogRingEntries;
    const unsigned long long first = total - kept;

    OSArray *lines = OSArray::withCapacity(static_cast<unsigned>(kept));
    if (lines != nullptr) {
        for (unsigned long long seq = first; seq < total; ++seq) {
            OSString *line = OSString::withCString(snapshot->lines[seq % kLogRingEntries]);
            if (line != nullptr) {
                lines->setObject(line);
                line->release();
            }
        }
        service->setProperty(kLogProperty, lines);
        lines->release();
    }
    service->setProperty(kLogDroppedProperty, first, 64);
    service->setProperty(kLogLevelProperty, static_cast<unsigned long long>(gLogLevel), 32);
    IOFree(snapshot, sizeof(LogRing));
}

} // namespace vv
