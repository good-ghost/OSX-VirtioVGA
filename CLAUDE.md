# Virtio-Video 프로젝트 규칙

전역 규칙(`~/.claude/CLAUDE.md`)을 따르고, 이 프로젝트에서는 아래를 추가로 지킨다.

## 문서

- 요구사항은 `docs/REQUIREMENT.md`가 기준이다. 요구사항 ID(`REQ-`, `NFR-`, `Q-`)는 바꾸거나 재사용하지 않는다.
- 계획 문서: `docs/PROJECT-PLAN.md`(WI 목록·체크리스트·추적 표), `docs/phases/PHASE-PLAN-nn.md`(Phase별 상세). Work Item ID는 `WI-p.nnn` 형식이고, 끼워 넣는 작업은 `WI-2.004b`처럼 파생 ID를 쓴다. 취소한 WI는 지우지 않고 취소로 표시한다.

## Work Item 진행 규칙

WI 하나를 끝낼 때 다음 순서를 지킨다.

1. **테스트**: 빌드(`scripts/build.sh`)와 단위 테스트(`scripts/test.sh`)의 종료 코드가 0이다. 실기 확인이 필요한 WI는 PHASE-PLAN의 수락 기준을 로그·ioreg·스크린샷으로 판정한다. 확인하지 못한 항목은 완료로 표시하지 않는다.
2. **문서 체크**: `docs/PROJECT-PLAN.md`의 상태 표와 WI 체크리스트에 `[X]`를 표시한다. 실측 결과·결정은 해당 PHASE-PLAN의 "결정 기록" 절에 적는다.
3. **커밋**: WI 하나에 커밋 하나. 형식은 `<type>(WI-p.nnn): <요약>` (예: `feat(WI-1.003): virtio feature 협상 구현`). 커밋은 사용자가 요청할 때만 한다.

## 커널 코드 안전 수칙

- kext는 커널에서 돈다. 잘못되면 VM이 부팅되지 않는다.
- **실행 중인 시스템에 kext를 로드(`kmutil load`)하거나 EFI/OpenCore 설정을 바꾸는 일은 매번 사용자에게 확인을 받는다.**
- kext는 `/Library/Extensions`(보조 kernel collection)에 설치하고 `sudo kmutil load -z -p`로 등록한 뒤 재부팅한다 (REQUIREMENT Q-01 v1.2, INSTALL.md §3A). OpenCore 주입은 IOGraphicsFamily 심볼을 쓰는 kext를 올리지 못한다(T-04). SIP는 `csr-active-config` 0x201(Kext Signing만 해제)에서 더 풀지 않는다.
- 대상은 macOS 15.x x86_64만이다 (Q-05). 라이선스는 MIT로 정했다 (Q-10).
- 설치 전에 롤백 경로(드라이버를 뺀 OpenCore 부팅 항목, 또는 호스트 쪽 디스크 스냅샷)가 준비됐는지 확인한다.
- 호스트(QEMU)가 넘겨주는 값(응답 길이, scanout 수, EDID 크기 등)은 모두 범위를 검사한 뒤 쓴다.
- Linux `drivers/gpu/drm/virtio` 등 GPL 코드는 복사하지 않는다. VirtIO 사양(OASIS)과 공개 문서를 근거로 구현한다.

## 비밀 정보

- sudo 암호, 서명 인증서, Apple 계정 정보는 저장소·로그·문서에 남기지 않는다. sudo가 필요한 단계는 사용자가 직접 실행한다.

## 빌드·검증

- 빌드: `scripts/build.sh [debug|release]` → `build/VirtioVideo.kext`. 종료 코드로 판정한다.
  - 링크 경고 "libkmod.a … built for newer 'macOS' version (26.2)"는 SDK 라이브러리 때문이며 알려진 경고다 (PHASE-PLAN-00 결정 기록).
- 의존성 확인: `kmutil libraries -p build/VirtioVideo.kext` (종료 코드 0), `kmutil print-diagnostics -p build/VirtioVideo.kext`에서 `Dependencies: OK`. 소유권·서명 오류는 OpenCore 주입 방식에서는 무시한다.
- bundle id와 버전은 `kext/Info.plist`에서만 바꾼다. build.sh가 읽어 `kmod_info`에 넣는다.
- build.sh는 kext가 쓰는 KPI가 `OSBundleLibraries`에 모두 선언됐는지 검사한다. 실패하면 메시지의 라이브러리를 Info.plist에 추가한다 (버전은 macOS 15 하한 `24.0`, 패밀리는 `1.0`).
- 단위 테스트: `scripts/test.sh` → `build/tests/vv-tests` (종료 코드 0 = 통과). 대상은 `kext/include/vv/`의 커널 비의존 코드이고 테스트는 `tests/*.cpp`에 `VV_TEST`로 쓴다. ASan·UBSan을 켜고 빌드한다.
- 사양 상수·구조체는 `kext/include/vv/VirtioSpec.hpp`, `VirtioGpuSpec.hpp`에만 둔다. 매직 넘버를 코드에 직접 쓰지 않는다.
- 드라이버 로그: 통합 로그에는 남지 않는다. `ioreg -r -c <클래스> -l -w0 | grep '"VV'`로 `VVLog` 속성을 읽는다. 수준은 boot-arg `vvideo_log=0|1|2`.
- 시스템 로그를 볼 때는 `/usr/bin/log`를 쓴다 (zsh에서 `log`는 내장 명령).
- 커널 패닉: `python3 -I scripts/panic-summary.py [보고서]`. build.sh가 dSYM을 `build/dsym-archive/<UUID>.dSYM`에 보관하므로 `build/`를 지우지 않는다. 자세한 절차는 docs/DEBUGGING.md.
- 드라이버 적용 전/후 비교: `scripts/collect-env.sh --save <label>`로 `docs/research/env-<시각>-<label>.txt`에 저장하고 `scripts/compare-env.sh <이전> <이후>`로 비교한다 (종료 코드 0 = 차이 없음, 1 = 차이 있음). 부팅마다 바뀌는 값은 비교에서 지운다.
- 화면 확인 결과는 `docs/screenshots/`, 녹화는 `docs/movie/`에 남긴다.
