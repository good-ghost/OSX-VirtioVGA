// 드라이버 전체에 적용되는 boot-arg (REQ-072).
#pragma once

namespace vv {

// 이 boot-arg가 있으면 모든 서비스가 probe()에서 매칭을 거절한다.
// 왜: 드라이버 결함으로 부팅이 안 될 때 kext 파일을 지우지 않고 OpenCore boot-args만 고쳐 복구하기 위해서다.
inline constexpr const char *kDisableBootArg = "-vvideooff";

// boot-arg가 있는지 확인한다. 값이 없는 플래그 형태(-xxx)를 확인한다.
bool isDriverDisabledByBootArg();

// GPU PCI 장치에 이 속성이 있으면 IONDRVFramebuffer::probe가 매칭을 거절한다 (KDK IONDRVSupport 디스어셈블로 확인).
// OpenCore DeviceProperties로 넣어 부팅 때부터 IONDRV가 붙지 않게 하고, 우리 프레임버퍼가 붙는다.
inline constexpr const char *kIgnoreNdrvProperty = "AAPL,iokit-ignore-ndrv";

// 본 kext가 시작에 실패해 IONDRV에 화면을 돌려줄 때 PCI 장치에 남기는 표시 (WI-2.008).
// 재매칭 때 보조 kext가 다시 probe되므로, 이 표시가 있으면 속성을 다시 붙이지 않는다 (2026-11-24 실측한 경쟁 조건).
inline constexpr const char *kNdrvHandedBackProperty = "VVNdrvHandedBack";

} // namespace vv
