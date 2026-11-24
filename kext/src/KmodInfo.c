// kext 모듈 정보(kmod_info). Xcode kext 타깃이 자동 생성하는 파일을 직접 둔다.
// 왜: 이 프로젝트는 .xcodeproj 없이 scripts/build.sh로 빌드하므로 (PHASE-PLAN-00 결정 기록).
// VV_BUNDLE_ID_STR, VV_VERSION_STR은 build.sh가 Info.plist에서 읽어 -D로 넘긴다.
#include <mach/mach_types.h>
#include <mach/kmod.h>

#ifndef VV_BUNDLE_ID_STR
#error "VV_BUNDLE_ID_STR must be defined by the build script"
#endif
#ifndef VV_VERSION_STR
#error "VV_VERSION_STR must be defined by the build script"
#endif

// libkmod.a가 제공하는 진입점. IOKit C++ kext는 _realmain/_antimain을 비워 둔다.
extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);

__attribute__((visibility("default")))
kmod_info_t kmod_info = {
    0, KMOD_INFO_VERSION, -1U,
    { VV_BUNDLE_ID_STR }, { VV_VERSION_STR },
    -1, 0, 0, 0, 0,
    _start, _stop
};

__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
