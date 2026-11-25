// VirtioGpuDevice 설정을 실행 중에 바꾸는 측정용 도구 (WI-4.004).
// 사용법: vvctl dirty <0|1> [tile]
// 드라이버는 root의 요청만 받는다 (VirtioGpuDevice::setProperties, kIOClientPrivilegeAdministrator).
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void)
{
    fprintf(stderr, "usage: vvctl dirty <0|1> [tile]\n");
    return 2;
}

static int parseUnsigned(const char *text, long *out)
{
    char *end = NULL;
    const long value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < 0) {
        return 0;
    }
    *out = value;
    return 1;
}

// VirtioGpuDevice는 registerService를 하지 않으므로(!registered) IOServiceGetMatchingService로는 찾을 수 없다.
// 서비스 평면 전체를 돌면서 클래스를 확인한다.
static io_service_t findGpuDevice(void)
{
    io_iterator_t iterator = IO_OBJECT_NULL;
    if (IORegistryCreateIterator(kIOMainPortDefault, kIOServicePlane, kIORegistryIterateRecursively, &iterator) !=
        KERN_SUCCESS) {
        return IO_OBJECT_NULL;
    }
    io_service_t found = IO_OBJECT_NULL;
    io_registry_entry_t entry;
    while ((entry = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        if (IOObjectConformsTo(entry, "VirtioGpuDevice")) {
            found = entry;
            break;
        }
        IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
    return found;
}

int main(int argc, char **argv)
{
    long enabled = 0;
    long tile = 0;
    if (argc < 3 || strcmp(argv[1], "dirty") != 0 || !parseUnsigned(argv[2], &enabled) || enabled > 1) {
        return usage();
    }
    if (argc > 3 && !parseUnsigned(argv[3], &tile)) {
        return usage();
    }
    io_service_t service = findGpuDevice();
    if (service == IO_OBJECT_NULL) {
        fprintf(stderr, "vvctl: VirtioGpuDevice not found\n");
        return 1;
    }
    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 2, &kCFTypeDictionaryKeyCallBacks,
                                                            &kCFTypeDictionaryValueCallBacks);
    int enabledValue = (int)enabled;
    CFNumberRef enabledNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &enabledValue);
    CFDictionarySetValue(dict, CFSTR("VVDirtyFlush"), enabledNumber);
    CFRelease(enabledNumber);
    if (tile != 0) {
        int tileValue = (int)tile;
        CFNumberRef tileNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &tileValue);
        CFDictionarySetValue(dict, CFSTR("VVDirtyTile"), tileNumber);
        CFRelease(tileNumber);
    }
    const kern_return_t result = IORegistryEntrySetCFProperties(service, dict);
    CFRelease(dict);
    IOObjectRelease(service);
    if (result != KERN_SUCCESS) {
        fprintf(stderr, "vvctl: set properties failed: 0x%x\n", result);
        return 1;
    }
    printf("dirty=%ld%s\n", enabled, tile != 0 ? " (tile set)" : "");
    return 0;
}
