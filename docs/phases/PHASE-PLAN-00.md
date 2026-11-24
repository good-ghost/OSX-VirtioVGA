# PHASE-PLAN-00 — 개발 환경

| 항목      | 내용                                                     |
| --------- | -------------------------------------------------------- |
| Phase     | P0                                                       |
| 기간      | 8.0 Man-day                                              |
| 관련 요구 | REQ-006, 064, 070~072, 075, NFR-010, 011, 012, 013       |
| 선행      | 없음                                                     |
| 완료 조건 | 빈 kext를 빌드해 OpenCore로 주입·부팅하고, 끄고 되돌리는 절차와 디버깅 절차가 문서로 있다 |

## 1. 목표

드라이버 코드를 쓰기 전에 **빌드 → 설치 → 부팅 → 로그 확인 → 롤백**이 한 바퀴 도는 환경을 만든다. 이후 Phase의 모든 시험은 이 절차를 반복한다. 커널 코드는 실패하면 VM이 부팅되지 않으므로 롤백 경로(WI-0.003)를 첫 설치 전에 준비한다.

## 2. 진행 방식

- AI가 스크립트·헤더·문서 초안을 만들고, 사용자가 EFI 변경·재부팅·sudo가 필요한 단계를 직접 실행한다.
- EFI `config.plist` 변경, `Kexts/` 복사, KDK 설치는 사용자가 한다. AI는 바꿀 내용을 정확히 제시한다.
- 각 WI의 수락 기준은 명령 출력(종료 코드)과 로그로 판정한다.

## 3. 의존 관계와 실행 순서

```
WI-0.008 ─┐
WI-0.006 ─┼─> WI-0.001 ─> WI-0.005 ─> WI-0.003 ─> WI-0.002 ─> WI-0.004
          │                                                └─> WI-0.007
```

1. WI-0.008, WI-0.006 (커널 없이 진행 가능, 병렬)
2. WI-0.001 → WI-0.005 → WI-0.003 (끄는 스위치까지 넣은 빈 kext)
3. WI-0.002 (첫 설치. 롤백 절차가 준비된 뒤)
4. WI-0.004, WI-0.007

## 4. Work Item 상세

### WI-0.001 빌드 체계 결정과 빈 kext 빌드 (1.5)

세부 작업

1. Xcode 26.3의 프로젝트 템플릿에 "Generic Kernel Extension"/"IOKit Driver"가 있는지 확인한다.
2. 템플릿이 있으면 `xcodebuild`, 없으면 `xcrun clang++ -mkernel` 기반 Makefile로 정한다. 결정과 이유를 이 문서 §6에 적는다.
3. 디렉터리 배치:

   ```
   kext/
     Info.plist
     src/        # 커널 코드 (.cpp/.hpp)
     include/    # 커널·사용자 공간 공용 헤더 (상수, 구조체, 순수 로직)
   scripts/build.sh
   ```

4. 빌드 설정(스케치 — 실제 플래그는 빌드 결과로 확정):
   - SDK `MacOSX26.2.sdk`, `MACOSX_DEPLOYMENT_TARGET=15.0`, `-arch x86_64`
   - `-mkernel -fno-exceptions -fno-rtti -nostdlib`, 링크 `-Xlinker -kext -lkmod -lkmodc++ -lcc_kext`
5. `Info.plist` 초안:

   ```xml
   <key>CFBundleIdentifier</key><string>(프로젝트 bundle id — WI-0.001에서 확정)</string>
   <key>OSBundleLibraries</key>
   <dict>
     <key>com.apple.iokit.IOGraphicsFamily</key><string>1.0</string>
     <key>com.apple.iokit.IOPCIFamily</key><string>1.0</string>
     <key>com.apple.kpi.iokit</key><string>…</string>
     <key>com.apple.kpi.libkern</key><string>…</string>
   </dict>
   ```

   이 단계에서는 `IOKitPersonalities`를 넣지 않는다 (매칭하지 않는 빈 kext).

수락 기준

- `scripts/build.sh` 종료 코드 0, `build/VirtioVideo.kext` 생성
- `kmutil libraries -p build/VirtioVideo.kext` 오류 없음 (의존성 버전 문자열은 이 출력으로 확정)
- CLAUDE.md "빌드·검증" 절에 빌드 명령 기록

### WI-0.002 OpenCore 주입으로 최소 kext 로드 확인 (1)

세부 작업

1. 시작 시 로그 한 줄만 남기는 `IOService` 하위 클래스를 `IOResources`에 매칭시킨다 (장치에는 아직 붙지 않는다).
2. 사용자에게 제시할 `config.plist > Kernel > Add` 항목:

   ```xml
   <dict>
     <key>Arch</key><string>x86_64</string>
     <key>BundlePath</key><string>VirtioVideo.kext</string>
     <key>Enabled</key><true/>
     <key>ExecutablePath</key><string>Contents/MacOS/VirtioVideo</string>
     <key>MinKernel</key><string>24.0.0</string>
     <key>PlistPath</key><string>Contents/Info.plist</string>
   </dict>
   ```

   위치는 Lilu·WhateverGreen 항목 뒤. 실제 OC 버전 스키마는 사용자의 config.plist로 확인한다.
3. 사용자가 EFI에 복사·재부팅한다.

수락 기준

- `kmutil showloaded | grep -i virtiovideo`에 보임
- `log show --last boot --predicate 'sender == "VirtioVideo"'`에 시작 로그
- `csrutil status`가 변경 전과 같음

### WI-0.003 롤백 절차와 비활성화 boot-arg (1)

세부 작업

1. `docs/INSTALL.md` 작성: 설치, OC 항목 `Enabled=false`, OC 부팅 선택 화면에서 다른 config로 부팅하는 방법(사용자 환경에 맞춰 확인), 호스트 스냅샷(`qemu-img snapshot -c`) 생성·복원.
2. boot-arg 검사(스케치):

   ```cpp
   // 왜: 드라이버 결함으로 부팅이 안 될 때 OC boot-args만 고쳐 복구하기 위해서 (REQ-072)
   static constexpr const char *kDisableBootArg = "-vvideooff";
   if (PE_parse_boot_argn(kDisableBootArg, &dummy, sizeof(dummy))) { return nullptr; }
   ```

수락 기준

- boot-arg를 넣고 부팅 → 시작 로그에 "disabled by boot-arg", 화면은 기존과 동일
- INSTALL.md 절차대로 kext를 끄고 부팅이 되는 것을 사용자가 한 번 실행해 확인

### WI-0.004 KDK·QEMU gdbstub 디버깅과 패닉 로그 수집 절차 (1.5)

세부 작업

1. 사용자가 Apple Developer에서 24H241(또는 가장 가까운 빌드) KDK를 받아 설치한다.
2. 호스트 QEMU에 `-s`(gdbstub)를 붙여 부팅하고, 호스트 또는 게스트에서 lldb로 접속하는 절차를 실제로 해 보고 `docs/DEBUGGING.md`에 적는다. 어느 쪽 lldb를 쓸지는 시험 결과로 정한다.
3. 패닉 보고서 위치(`/Library/Logs/DiagnosticReports/*.panic`)와 kext 슬라이드 주소로 심볼을 찾는 방법을 적는다.

수락 기준

- kext의 `start()`에 중단점을 걸어 멈추는 것을 한 번 확인 (실패하면 원인과 대안 — 로그 기반 — 을 기록)
- DEBUGGING.md가 처음 보는 사람도 따라 할 수 있는 순서로 되어 있다

### WI-0.005 로깅 매크로와 로그 수준 (0.5)

- `os_log` 계열 또는 `IOLog` 중 커널 kext에서 쓸 수 있는 것을 확인해 정한다.
- 매크로 `VV_LOG_ERR / VV_LOG_INFO / VV_LOG_DBG`, 수준은 boot-arg `vvideo_log=<0..2>` 또는 Info.plist 값.
- 수락 기준: 수준 0/2에서 디버그 로그가 각각 안 나오고/나오는 것을 로그로 확인.

### WI-0.006 사용자 공간 단위 테스트 하네스 (1)

- `kext/include/`의 순수 로직(구조체 크기, 링 인덱스, 모드 계산, EDID 검사)을 macOS 사용자 공간에서 `clang++ -std=c++17`로 빌드해 실행한다.
- 새 의존성을 넣지 않고 간단한 assert 기반 러너로 시작한다. 프레임워크가 필요해지면 그때 이유와 함께 제안한다.
- 수락 기준: `scripts/test.sh` 종료 코드 0, 일부러 실패시킨 테스트에서 0이 아님.

### WI-0.007 환경 수집 스크립트 비교 모드 (0.5)

- `collect-env.sh`에 이 드라이버 클래스의 IORegistry 속성 수집을 추가한다.
- 날짜별 결과를 `docs/research/`에 저장하고 `diff`로 비교하는 절차를 README 또는 스크립트 옵션으로 둔다.

### WI-0.008 virtio / virtio-gpu 상수·구조체 헤더 (1)

- 대상: device status 비트, feature 비트, PCI capability(`virtio_pci_cap`, `virtio_pci_common_cfg`, notify cap), split virtqueue 구조체, virtio-gpu 제어 헤더·명령/응답·config 구조체.
- OASIS VIRTIO 1.2 사양 원문을 보고 작성한다. Linux·QEMU 헤더를 복사하지 않는다 (NFR-012).
- 수락 기준: 모든 구조체에 `static_assert(sizeof(...) == N)`, 상수마다 사양 절 번호 주석, 단위 테스트 통과.

## 5. 리스크

| 리스크                                    | 대응                                                     |
| ----------------------------------------- | -------------------------------------------------------- |
| Xcode 26에서 kext 빌드 지원이 줄었을 수 있음 | Makefile 방식으로 전환 (WI-0.001)                        |
| KDK를 구할 수 없음 (R-07)                  | 로그 기반 디버깅으로 진행, 결과를 DEBUGGING.md에 기록    |
| 첫 주입에서 부팅 실패                      | WI-0.003 절차, 호스트 스냅샷                             |

## 6. 결정 기록

### WI-0.001 빌드 체계 (2026-11-23)

| 항목 | 결정·실측 |
| ---- | --------- |
| Xcode kext 템플릿 | Xcode 26.3에 `IOKit Driver.xctemplate`, `Generic Kernel Extension.xctemplate`가 **있다** (`Platforms/MacOSX.platform/Developer/Library/Xcode/Templates/Project Templates/macOS/Other/`) |
| 빌드 방식 | 템플릿이 있지만 **`scripts/build.sh`(clang 직접 호출)** 로 정했다. `.xcodeproj`를 손으로 만들거나 관리하지 않고 CLI만으로 재현하기 위해서다. Xcode kext 타깃이 자동 생성하는 `kmod_info`는 `kext/src/KmodInfo.c`로 직접 둔다 |
| 툴체인 | Apple clang 17.0.0 (clang-1700.6.4.2), SDK `MacOSX26.2.sdk`, `-mmacosx-version-min=15.0`, `-arch x86_64` |
| 컴파일 플래그 | `-mkernel -nostdinc -fno-builtin -fno-common -DKERNEL -Wall -Wextra -Werror`, C++는 `-std=c++17 -fapple-kext -fno-exceptions -fno-rtti` |
| 링크 | `-mkernel -nostdlib -Xlinker -kext -lkmodc++ -lkmod -lcc_kext` |
| bundle id | `local.virtiovideo.VirtioVideo` — 개인 사용(Q-06)이라 실제 도메인이 아닌 `local.` 접두사를 썼다. 공개 배포 시 바꾼다. `Info.plist`가 유일한 출처이고 build.sh가 읽어 `kmod_info`에 넣는다 |
| 버전 | `0.0.1` |
| OSBundleLibraries | `com.apple.kpi.iokit` 24.0, `com.apple.kpi.libkern` 24.0 (`kmutil libraries -p --xml` 권장값 24.6.0을 macOS 15.0 하한에 맞춰 24.0으로), `com.apple.iokit.IOGraphicsFamily` 1.0, `com.apple.iokit.IOPCIFamily` 1.0 (두 패밀리 모두 `OSBundleCompatibleVersion` 1.0.0b1 확인) |
| 검증 | `scripts/build.sh` 종료 코드 0, `file` → `Mach-O 64-bit kext bundle x86_64`, `kmutil libraries -p` 종료 코드 0·미해결 심볼 없음, `kmutil print-diagnostics` → `Dependencies: OK` |

알려진 경고·미확인 사항

- 링크 시 `libkmod.a`의 `c_start.o`/`c_stop.o`가 "macOS 26.2용으로 빌드됨(링크 대상 15.0)" 경고가 난다. SDK에 들어 있는 라이브러리 자체의 빌드 버전이라 이 프로젝트에서 바꿀 수 없다. 실제 15.8.3에서 로드되는지는 WI-0.002에서 확인한다.
- 바이너리에 `LC_BUILD_VERSION` 로드 명령이 없다 (`LC_UUID`, `LC_SOURCE_VERSION`, 세그먼트만 있음). 로드에 문제가 되는지는 WI-0.002에서 확인한다.
- `kmutil print-diagnostics`는 소유권(501:20, root:wheel 아님)과 코드 서명 없음을 오류로 보고한다. 이는 `kmutil load` 경로의 조건이고, OpenCore 주입(Q-01)에는 해당하지 않는 것으로 판단한다. WI-0.002에서 실제 주입으로 확인한다.

### WI-0.002 첫 주입 (2026-11-23, 재부팅 전 상태)

| 항목 | 내용 |
| ---- | ---- |
| 사전 조건 | 사용자가 호스트에서 디스크 스냅샷 생성 완료. EFI는 사용자가 `sudo diskutil mount disk0s1`로 마운트 |
| 사용 중인 EFI 판단 | `disk0s1`(유일한 EFI). OpenCore `REL-106-2025-11-03` 실행 중. config의 `Kernel > Add` 중 폴더에 실제로 있는 Lilu·VirtualSMC·WhateverGreen만 로드되어 있어, 이 config가 현재 부팅에 쓰인 것으로 판단 (재부팅 후 kext 로드로 최종 확인) |
| 백업 | EFI 안 `EFI/OC/config.plist.bak-20261123-wi0002`, 로컬 `docs/research/efi/config.plist.before-wi0002` (git 제외 — SMBIOS 정보 포함) |
| 복사 | `build/VirtioVideo.kext` → `EFI/OC/Kexts/VirtioVideo.kext` (`ditto --norsrc --noextattr`, `dot_clean -m`으로 `._*` 제거, `diff -r` 동일 확인) |
| config 변경 | `Kernel > Add` 마지막(인덱스 8)에 1개 항목 추가: `Arch=x86_64`, `BundlePath=VirtioVideo.kext`, `ExecutablePath=Contents/MacOS/VirtioVideo`, `PlistPath=Contents/Info.plist`, `MinKernel=24.0.0`, `Enabled=true`. 변경 전후 diff는 이 항목 하나뿐. `plutil -lint` OK |
| 이 단계의 kext | `IOResources`에 매칭해 시작 로그만 남기는 `VirtioVideoProbe`. PCI 장치·화면에는 붙지 않는다 |

롤백 방법 (부팅이 안 될 때)

1. 호스트에서 만든 디스크 스냅샷으로 되돌린다. 또는
2. OC 부팅 선택 화면에서 Recovery로 부팅 → 터미널에서 EFI 마운트 → `EFI/OC/config.plist.bak-20261123-wi0002`를 `config.plist`로 덮어쓴다.

기존 config에서 발견한 사항 (이 작업과 무관, 수정하지 않음)

- `Kernel > Add`의 `VoodooPS2Controller.kext`(2개 항목), `AppleMCEReporterDisabler.kext`, `USBToolBox.kext`, `UTBMap.kext`는 `EFI/OC/Kexts/`에 파일이 없다. 반대로 폴더의 `MCEReporterDisabler.kext`, `USBPorts.kext`, `AppleALC.kext` 등은 config에 없다. 현재 부팅에는 영향이 없어 보이지만 사용자 확인이 필요하다.

### WI-0.002 재부팅 후 확인 (2026-11-23 21:57)

| 확인 | 결과 |
| ---- | ---- |
| 로드 | `kmutil showloaded`에 `local.virtiovideo.VirtioVideo (0.0.1)` 인덱스 118, 의존 `<113 18 6 3>` (113 = IOGraphicsFamily) |
| 서비스 객체 | `ioreg -c VirtioVideoProbe` → `active` (IOResources 아래) |
| SIP | `enabled` — 변경 없음 |
| 화면 | 변경 없음 (`IONDRVFramebuffer`, 1280×800, VRAM 3 MB) |
| 시작 로그 | **찾지 못함.** `IOLog("VirtioVideo: probe service started")`가 `/usr/bin/log show --last boot`에 나오지 않는다 |
| 링크 경고·`LC_BUILD_VERSION` 없음 | 로드에 문제 없음 (WI-0.001 미확인 사항 해소) |

부팅 로그에서 확인한 사실

- 부팅 초기(21:56:21~27)에 15회 "library kext com.apple.iokit.IOGraphicsFamily not found → failed to resolve library dependencies (0xdc00800e)"가 기록된 뒤, IOGraphicsFamily가 올라온 다음 로드에 성공했다.
- 해석(추정): OpenCore가 주입한 kext는 부트 kernel collection에 들어간다. `IOPCIFamily`는 부트 collection(주소 `0xffffff80…`)에 있지만 `IOGraphicsFamily`는 시스템 collection(주소 `0xffffff7f…`)에 있다. 그래서 IOGraphicsFamily에 의존하는 이 kext는 시스템 collection이 올라온 뒤에야 로드된다.

후속 조치

- **T-04 (새 확인 사항)**: 실제 프레임버퍼 드라이버도 IOGraphicsFamily에 의존하므로 같은 시점에 로드된다. 부트 프레임버퍼 대체(WI-2.006)와 부트 화면 이어받기(WI-2.009)에서 이 로드 시점이 문제가 되는지 확인한다. 반복 실패 로그는 동작에 영향이 없지만, P0~P1 동안 IOGraphicsFamily가 필요 없으면 의존성을 빼서 부트 단계에 로드되는지 비교할 수 있다.
- **시작 로그**: IOLog가 통합 로그에 남지 않는 원인은 확인하지 못했다. WI-0.005(로깅 매크로)에서 `os_log` 계열과 비교해 정하고, 그때 이 체크 항목을 다시 확인한다.
- **환경 주의**: 이 셸(zsh)에서 `log`는 zsh 내장 명령이라 `log show`가 "too many arguments"로 실패한다. 반드시 `/usr/bin/log`를 쓴다.

### WI-0.005 로그 출력 경로와 수준 (2026-11-23)

실측 (OpenCore 주입 kext, macOS 15.8.3)

| 시험 | 결과 |
| ---- | ---- |
| 0.0.2: `IOLog`와 `os_log_with_type(OS_LOG_DEFAULT)` 두 경로로 기록 | `start()` 호출 확인(`VVProbeStarted=Yes`), boot-arg 수준 읽기 확인(`VVLogLevel=2`). 그러나 **통합 로그(`/usr/bin/log show --last boot`)와 `sudo dmesg` 어디에도 메시지가 없었다.** kext 이미지 UUID로 검색해도 0건 |
| 0.0.3: 내부 링 버퍼 → IORegistry `VVLog` | `vvideo_log=2`: E·I·D 세 줄 모두 보임. `vvideo_log=0`: E 한 줄만 보임. `VVLogDropped=0` |

결정

- **주 출력 경로는 드라이버 내부 링 버퍼(128줄 × 160바이트)이고, IORegistry 속성 `VVLog`로 공개한다.** 읽기: `ioreg -r -c <클래스> -l -w0 | grep '"VV'`.
- `os_log`에도 함께 보낸다. 지금은 보이지 않지만 비용이 작고, kext가 디스크 kernel collection에 설치되는 경우 보일 수 있다. `IOLog`는 뺐다.
- 수준: boot-arg `vvideo_log=<0|1|2>` (0 오류, 1 정보 — 기본, 2 디버그). 범위 밖 값은 기본값. 값은 `VVLogLevel` 속성으로도 공개한다.
- 사용 KPI: `PE_parse_boot_argn`, `IOSimpleLock*`, `IOMallocZero`(kpi.iokit), `vsnprintf`, `snprintf`, `_os_log_internal`(kpi.libkern) — 모두 공개 KPI.
- 제약: 인터럽트(필터) 컨텍스트에서 로그 매크로를 쓰지 않는다. `publishLog()`는 `setProperty`를 하므로 워크루프·스레드 컨텍스트에서만 부른다. P1 이후에는 주기 타이머나 상태 변화 시점에 `publishLog()`를 부른다.
- 메시지가 통합 로그에 남지 않는 원인은 확인하지 못했다. OpenCore가 주입한 kext는 logd가 포맷 문자열을 읽을 이미지 정보(uuidtext)를 찾지 못해 버리는 것으로 추정한다.

WI-0.003에서 boot-args를 `keepsyms=1`로 되돌렸다.

### WI-0.003 비활성화 스위치와 롤백 절차 (2026-11-23)

| 시험 | 설정 | 결과 |
| ---- | ---- | ---- |
| boot-arg | kext 0.0.4, boot-args `keepsyms=1 -vvideooff` | kext 로드됨(0.0.4), `VirtioVideoProbe` 객체 0개, 화면 `IONDRVFramebuffer` 1280×800 그대로 |
| Enabled=false | boot-args `keepsyms=1`, `Kernel > Add:8:Enabled=false` | kext 로드 안 됨, 객체 0개, `IONDRVFramebuffer` 그대로, SIP enabled |

- `-vvideooff`는 `vv::isDriverDisabledByBootArg()`(`kext/src/VVBootArgs.*`)로 검사하고, 서비스 클래스의 `probe()`에서 `nullptr`을 돌려준다. P2의 프레임버퍼 클래스도 같은 함수를 쓴다.
- 거절된 객체는 바로 해제되어 로그를 남길 곳이 없다. 확인은 ioreg에 객체가 없는 것으로 한다.
- OpenCore가 `NVRAM > Delete`로 boot-args를 매 부팅 다시 쓰므로, boot-args는 반드시 config.plist에서 바꾼다 (`nvram` 명령은 다음 부팅에 덮어써짐). INSTALL.md §4.1에 적었다.
- 시험하지 않은 것: Recovery로 부팅해 백업 config를 복원하는 절차(INSTALL.md §5.1)와 호스트 스냅샷 복원(§5.2). 실제 장애 때 처음 쓰게 된다.

현재 상태: boot-args `keepsyms=1`, VirtioVideo 항목 `Enabled=false` (kext 로드 안 됨). 다음에 kext가 필요한 WI(WI-0.004 등)에서 `Enabled=true`로 되돌린다.

### WI-0.006 단위 테스트 하네스 (2026-11-23)

- `scripts/test.sh`: `xcrun clang++ -isysroot <SDK> -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -I kext/include -I tests tests/*.cpp` → `build/tests/vv-tests` 실행.
- 러너는 `tests/TestMain.{hpp,cpp}`의 `VV_TEST`/`VV_EXPECT`(새 의존성 없음). 실패가 있으면 종료 코드 1.
- 커널 비의존 코드는 `kext/include/vv/`에 둔다. 로그 수준 해석(`vv/LogLevel.hpp`)을 커널 헤더에서 분리해 이곳으로 옮겼다.
- 확인: 11개 테스트 통과(종료 코드 0). 일부러 실패하는 테스트를 넣었을 때 종료 코드 1, 제거 후 0.

### WI-0.008 virtio / virtio-gpu 상수·구조체 헤더 (2026-11-23)

- 파일: `kext/include/vv/VirtioSpec.hpp`(장치 상태, 예약 feature 비트, split virtqueue, PCI capability·common cfg·notify·ISR), `kext/include/vv/VirtioGpuSpec.hpp`(GPU feature, config, ctrl type, 헤더, 2D·EDID·커서 구조체, 픽셀 형식).
- 출처: OASIS VIRTIO 1.2 cs01 HTML(§2.1, §2.7, §4.1). 이 HTML은 가져오는 중 §5.6에서 잘려, §5.7 GPU와 §6 예약 feature 비트는 사양 LaTeX 소스 GitHub `oasis-tcs/virtio-spec` **master**에서 옮겼다 (`virtio-1.2-cs01` 태그는 없었다). master는 1.2 이후 내용(GPU `BLOB_ALIGNMENT`, config `blob_alignment`, `ADMIN_VQ`, `SUSPEND`)을 포함하며, 이 드라이버가 쓰지 않는 것이라 헤더에 넣지 않았다.
- Linux·QEMU 헤더는 보지 않았다 (NFR-012).
- 모든 구조체 크기와 `virtio_pci_common_cfg` 필드 오프셋을 `static_assert`로 고정했다. 커널 빌드에서도 확인되도록 `kext/src/VirtioSpecCheck.cpp`가 두 헤더를 포함한다.
- 사양 확인으로 바로잡은 계획 문서 내용:
  - 픽셀 형식: `B8G8R8A8_UNORM = 1`, `B8G8R8X8_UNORM = 2`. PHASE-PLAN-02 초안은 반대로 적혀 있었다 → 수정.
  - 커서 명령도 응답(`RESP_OK_NODATA`)이 온다. PHASE-PLAN-04 초안의 "응답 없음"을 수정.
- 사양에 enum은 각 그룹 첫 값만 적혀 있어, 나머지 값은 C enum 순서로 계산했다. 실제 장치 응답으로 Phase 1~2에서 다시 확인한다.

### WI-0.007 환경 수집 비교 (2026-11-23)

- `scripts/collect-env.sh --save <label>`: 결과를 `docs/research/env-<YYYYMMDD-HHMMSS>-<label>.txt`에도 저장한다. 새 절 "VirtioVideo 드라이버 서비스"는 `kext/Info.plist`의 `IOKitPersonalities`에 있는 IOClass마다 ioreg 서비스 줄과 `VV*` 속성을 수집한다 (객체가 없으면 "(객체 없음)"). 클래스가 늘어나도 스크립트를 고칠 필요가 없다.
- grep이 아무것도 못 찾았을 때(종료 코드 1) 수집이 멈추지 않도록 했다.
- `scripts/compare-env.sh <이전> <이후>`: 수집 시각, uptime, kext 로드 인덱스·주소·참조 수·의존 목록, ioreg 객체 id·retain·busy 값을 지운 뒤 `diff -u`. 종료 코드는 diff와 같다.
- 확인 (종료 코드 기준):
  - 같은 상태에서 두 번 수집 → 비교 0 (차이 없음)
  - kext 인덱스·주소·retain만 바꾼 모의 재부팅 파일 → 0
  - VRAM 값만 바꾼 파일 → 1, 해당 줄만 diff에 나옴
  - 없는 파일 → 2
- 기준 수집본: `docs/research/env-20261123-225333-wi0007-a.txt` (VirtioVideo `Enabled=false` 상태). 최초 기준본 `2026-11-23-env-before.txt`는 이전 형식이라 비교하면 머리말·새 절 차이가 함께 나온다.
- 확인하지 못한 것: 드라이버 객체가 있을 때의 `VV*` 속성 수집은 kext가 비활성 상태라 이번에 실행해 보지 못했다. 다음에 kext를 켜는 WI에서 확인한다.

> 이 문서의 코드·설정 예시는 의도를 보여 주는 스케치다. 구현할 때는 SDK 헤더, 실제 빌드 출력, 사용자의 OpenCore 설정이 우선한다.
