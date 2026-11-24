// 드라이버 전체에 적용되는 boot-arg (REQ-072).
#pragma once

namespace vv {

// 이 boot-arg가 있으면 모든 서비스가 probe()에서 매칭을 거절한다.
// 왜: 드라이버 결함으로 부팅이 안 될 때 kext 파일을 지우지 않고 OpenCore boot-args만 고쳐 복구하기 위해서다.
inline constexpr const char *kDisableBootArg = "-vvideooff";

// boot-arg가 있는지 확인한다. 값이 없는 플래그 형태(-xxx)를 확인한다.
bool isDriverDisabledByBootArg();

} // namespace vv
