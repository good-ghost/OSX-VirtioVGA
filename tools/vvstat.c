// VirtioVGA 상태 조회 (WI-5.008). IORegistry 속성만 읽는다. user client를 열지 않고 아무것도 쓰지 않는다.
//
// 사용법: vvstat [--log N] [--all]
//   기본: 드라이버 버전과 주요 상태 요약
//   --log N: 드라이버 로그(VVLog)의 마지막 N줄도 출력
//   --all: VirtioGpuDevice와 VirtioVideoFramebuffer의 VV* 속성을 모두 출력
// 종료 코드: 0 = 드라이버가 붙어 동작 중(VVInitResult driver-ok), 1 = 찾지 못했거나 동작 중이 아님, 2 = 사용법 오류
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/kext/KextManager.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kTextBytes = 1024, kDefaultLogLines = 20 };

// 요약에 보여 줄 속성과 제목. 순서대로 출력한다.
static const char *const kSummaryKeys[][2] = {
    {"VVInitResult", "init"},
    {"VVCurrentMode", "mode"},
    {"VVPreferredMode", "preferred"},
    {"VVScanout", "scanout"},
    {"VVDisplayPower", "display power"},
    {"VVCursor", "cursor"},
    {"VVDirtyStats", "dirty flush"},
    {"VVAutoResizeState", "auto resize"},
    {"VVEdid", "edid"},
    {"VVEventCounters", "events"},
    {"VVRecoveries", "recoveries"},
    {"VVIrqMode", "irq"},
    {"VVNegotiation", "features"},
};

// VirtioGpuDevice는 registerService를 하지 않아 IOServiceGetMatchingService로 찾을 수 없다. 서비스 평면을 돈다.
static io_registry_entry_t findEntry(const char *className)
{
    io_iterator_t iterator = IO_OBJECT_NULL;
    if (IORegistryCreateIterator(kIOMainPortDefault, kIOServicePlane, kIORegistryIterateRecursively, &iterator) !=
        KERN_SUCCESS) {
        return IO_OBJECT_NULL;
    }
    io_registry_entry_t found = IO_OBJECT_NULL;
    io_registry_entry_t entry;
    while ((entry = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        if (IOObjectConformsTo(entry, className)) {
            found = entry;
            break;
        }
        IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
    return found;
}

// 속성 값을 한 줄 문자열로 바꾼다. 배열은 원소 수만, 데이터는 길이만 적는다.
static void formatValue(CFTypeRef value, char *out, size_t capacity)
{
    if (value == NULL) {
        snprintf(out, capacity, "-");
    } else if (CFGetTypeID(value) == CFStringGetTypeID()) {
        if (!CFStringGetCString((CFStringRef)value, out, (CFIndex)capacity, kCFStringEncodingUTF8)) {
            snprintf(out, capacity, "(string)");
        }
    } else if (CFGetTypeID(value) == CFNumberGetTypeID()) {
        long long number = 0;
        CFNumberGetValue((CFNumberRef)value, kCFNumberLongLongType, &number);
        snprintf(out, capacity, "%lld", number);
    } else if (CFGetTypeID(value) == CFBooleanGetTypeID()) {
        snprintf(out, capacity, "%s", CFBooleanGetValue((CFBooleanRef)value) ? "true" : "false");
    } else if (CFGetTypeID(value) == CFDataGetTypeID()) {
        snprintf(out, capacity, "(%ld bytes)", (long)CFDataGetLength((CFDataRef)value));
    } else if (CFGetTypeID(value) == CFArrayGetTypeID()) {
        snprintf(out, capacity, "(%ld items)", (long)CFArrayGetCount((CFArrayRef)value));
    } else {
        snprintf(out, capacity, "(unsupported type)");
    }
}

static CFTypeRef copyProperty(io_registry_entry_t entry, const char *key)
{
    CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
    CFTypeRef value = IORegistryEntryCreateCFProperty(entry, name, kCFAllocatorDefault, 0);
    CFRelease(name);
    return value;
}

static void printProperty(io_registry_entry_t entry, const char *key, const char *title)
{
    char text[kTextBytes];
    CFTypeRef value = copyProperty(entry, key);
    formatValue(value, text, sizeof(text));
    printf("  %-14s %s\n", title, text);
    if (value != NULL) {
        CFRelease(value);
    }
}

static bool initResultOk(io_registry_entry_t entry)
{
    char text[kTextBytes];
    CFTypeRef value = copyProperty(entry, "VVInitResult");
    formatValue(value, text, sizeof(text));
    if (value != NULL) {
        CFRelease(value);
    }
    return strcmp(text, "driver-ok") == 0;
}

static void printLog(io_registry_entry_t entry, long lines)
{
    CFTypeRef value = copyProperty(entry, "VVLog");
    if (value == NULL || CFGetTypeID(value) != CFArrayGetTypeID()) {
        printf("log: (none)\n");
        if (value != NULL) {
            CFRelease(value);
        }
        return;
    }
    const CFIndex count = CFArrayGetCount((CFArrayRef)value);
    const CFIndex first = count > lines ? count - lines : 0;
    printf("log (last %ld of %ld):\n", (long)(count - first), (long)count);
    char text[kTextBytes];
    for (CFIndex i = first; i < count; ++i) {
        formatValue(CFArrayGetValueAtIndex((CFArrayRef)value, i), text, sizeof(text));
        printf("  %s\n", text);
    }
    CFRelease(value);
}

// VV로 시작하는 속성을 이름 순서 없이 모두 출력한다.
static void printAll(io_registry_entry_t entry, const char *label)
{
    CFMutableDictionaryRef properties = NULL;
    if (IORegistryEntryCreateCFProperties(entry, &properties, kCFAllocatorDefault, 0) != KERN_SUCCESS ||
        properties == NULL) {
        printf("%s: (no properties)\n", label);
        return;
    }
    printf("%s:\n", label);
    const CFIndex count = CFDictionaryGetCount(properties);
    const void **keys = calloc((size_t)count, sizeof(void *));
    const void **values = calloc((size_t)count, sizeof(void *));
    if (keys != NULL && values != NULL) {
        CFDictionaryGetKeysAndValues(properties, keys, values);
        char name[kTextBytes];
        char text[kTextBytes];
        for (CFIndex i = 0; i < count; ++i) {
            if (!CFStringGetCString((CFStringRef)keys[i], name, sizeof(name), kCFStringEncodingUTF8) ||
                strncmp(name, "VV", 2) != 0) {
                continue;
            }
            formatValue(values[i], text, sizeof(text));
            printf("  %s = %s\n", name, text);
        }
    }
    free(keys);
    free(values);
    CFRelease(properties);
}

// 올라간 kext의 버전을 읽는다 (KextManager, 읽기 전용).
static void printKextVersion(void)
{
    const void *ids[] = {CFSTR("local.virtiovga.VirtioVGA")};
    CFArrayRef identifiers = CFArrayCreate(kCFAllocatorDefault, ids, 1, &kCFTypeArrayCallBacks);
    CFDictionaryRef loaded = KextManagerCopyLoadedKextInfo(identifiers, NULL);
    CFRelease(identifiers);
    char version[kTextBytes] = "not loaded";
    if (loaded != NULL) {
        CFDictionaryRef info = CFDictionaryGetValue(loaded, ids[0]);
        if (info != NULL && CFGetTypeID(info) == CFDictionaryGetTypeID()) {
            formatValue(CFDictionaryGetValue(info, CFSTR("CFBundleVersion")), version, sizeof(version));
        }
        CFRelease(loaded);
    }
    printf("VirtioVGA kext: %s\n", version);
}

int main(int argc, char **argv)
{
    long logLines = 0;
    bool all = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--all") == 0) {
            all = true;
        } else if (strcmp(argv[i], "--log") == 0) {
            logLines = kDefaultLogLines;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                char *end = NULL;
                logLines = strtol(argv[++i], &end, 10);
                if (end == NULL || *end != '\0' || logLines <= 0) {
                    fprintf(stderr, "usage: vvstat [--log N] [--all]\n");
                    return 2;
                }
            }
        } else {
            fprintf(stderr, "usage: vvstat [--log N] [--all]\n");
            return 2;
        }
    }

    io_registry_entry_t gpu = findEntry("VirtioGpuDevice");
    if (gpu == IO_OBJECT_NULL) {
        printf("VirtioGpuDevice: not found (driver not loaded, or disabled with -vvideooff)\n");
        return 1;
    }
    io_registry_entry_t framebuffer = findEntry("VirtioVideoFramebuffer");

    printKextVersion();
    printf("framebuffer: %s\n", framebuffer != IO_OBJECT_NULL ? "attached" : "not attached (IONDRV or none)");
    for (size_t i = 0; i < sizeof(kSummaryKeys) / sizeof(kSummaryKeys[0]); ++i) {
        printProperty(gpu, kSummaryKeys[i][0], kSummaryKeys[i][1]);
    }
    if (logLines > 0) {
        printLog(gpu, logLines);
    }
    if (all) {
        printAll(gpu, "VirtioGpuDevice");
        if (framebuffer != IO_OBJECT_NULL) {
            printAll(framebuffer, "VirtioVideoFramebuffer");
        }
    }
    const bool ok = initResultOk(gpu) && framebuffer != IO_OBJECT_NULL;
    if (framebuffer != IO_OBJECT_NULL) {
        IOObjectRelease(framebuffer);
    }
    IOObjectRelease(gpu);
    return ok ? 0 : 1;
}
