# PHASE-PLAN-05 — 안정화와 문서

| 항목      | 내용                                                     |
| --------- | -------------------------------------------------------- |
| Phase     | P5                                                       |
| 기간      | 9.5 Man-day                                              |
| 관련 요구 | REQ-070, 074, NFR-001~009, 012, AC-01~09                 |
| 선행      | P4 완료                                                  |
| 완료 조건 | **M3** — AC-01~AC-09 결과 표와 시연 녹화가 있고, 설치·디버깅 문서가 최신이다 |

## 1. 목표

기능 개발을 멈추고 시험과 문서로 안정 버전을 만든다. 이 Phase에서 발견한 결함은 해당 Phase의 파생 WI(예: `WI-3.002b`)로 고친다.

## 2. 실행 순서

```
WI-5.005 ─> WI-5.006 ─> WI-5.002 ─> WI-5.001 (장시간, 백그라운드)
WI-5.003, WI-5.004, WI-5.008 (WI-5.001 진행 중 병행 가능한 것만)
WI-5.007 ─> WI-5.009 ─> WI-5.010
```

- WI-5.001(24시간)은 VM을 계속 써야 하므로, 그동안 kext를 바꾸는 작업(WI-5.003)과 겹치지 않게 일정을 잡는다.

## 3. Work Item 상세

### WI-5.001 24시간 연속 사용 시험 (1.5)

- 시나리오: 일반 작업(브라우저, 터미널, 창 이동)과 동영상 재생을 섞는다. 디스플레이 잠자기가 여러 번 일어나게 둔다.
- 기록: 시작·종료 시각, 패닉 0회 확인(`/Library/Logs/DiagnosticReports`), 드라이버 오류 수(IORegistry), 메모리 사용량.

### WI-5.002 모드 전환 100회·누수 시험 (1)

- 자동화: 모드 전환을 스크립트로 반복한다. 사용할 수단(`displayplacer` 같은 외부 도구는 새 의존성이므로 사용자 승인 필요, 또는 CoreGraphics API를 쓰는 작은 Swift 스크립트)을 정한다. 기본안은 새 의존성 없이 Swift 스크립트.
- 판정: 패닉·정지 0회, 전환 전후 `ioclasscount`/`zprint`의 드라이버 관련 값 증가 없음.

### WI-5.003 언로드·재로드 가능성 확인 (1)

- OC로 주입된 kext를 SIP 켠 상태에서 `kmutil unload`할 수 있는지 확인 (T-02).
- 가능: 언로드 시 자원 해제 확인(`ioclasscount` 0), 재로드 성공.
- 불가: NFR-003을 "재부팅으로 교체"로 바꾸자는 변경 제안을 사용자에게 올린다 (요구사항은 승인 후 변경).

### WI-5.004 성능 측정 (1)

- NFR-004: 1920×1080, 60 Hz에서 드라이버 적용 전후 CPU 사용률 차이. 목표 한 코어의 10% 이하.
- NFR-005: 마우스 이동·창 드래그를 IONDRV(boot-arg로 끈 상태)와 녹화로 비교.

### WI-5.005 보안 경계 점검 (1)

- 호스트가 주는 값(응답 길이, `num_scanouts`, EDID 크기, display info 크기, capability 범위)의 검사 위치를 코드에서 목록으로 만들고 빠진 곳이 없는지 확인한다.
- user client 클래스가 없음을 확인한다 (NFR-007).

### WI-5.006 호환성 점검 (0.5)

- Lilu 1.6.8, WhateverGreen 1.6.7 로드 상태에서 부팅·모드 전환 정상.
- WhateverGreen이 이 장치에 속성을 주입하거나 패치하는지 부팅 로그로 확인.
- macOS 15.x 업데이트가 있으면 업데이트 전 스냅샷 후 시험 (R-05).

### WI-5.007 배경화면·투명 효과 전후 비교 기록 (1)

AC-07 다섯 항목을 같은 방법으로 측정한다 (REQUIREMENT §2.5와 같은 픽셀 측정).

| 항목                         | 적용 전 (2026-11-23)                | 적용 후 |
| ---------------------------- | ----------------------------------- | ------- |
| ① 정적 사진(Sonoma.heic)     | 정상                                |         |
| ② Sequoia 배경               | Cyan 단색 대체                      |         |
| ③ 단색 배경 16 px 테두리     | 있음                                |         |
| ④ 메뉴 막대·Dock 투명 효과   | 없음 (RGB 218/200 불투명)           |         |
| ⑤ `MTLCopyAllDevices()`      | 빈 목록                             |         |

②④⑤는 개선을 기대하지 않는다 (Q-12). 결과만 기록한다.

### WI-5.008 상태 조회 CLI (1)

- `tools/`에 IORegistry 속성만 읽는 작은 도구(Swift 또는 C, `IOKit.framework`)를 둔다. user client는 쓰지 않는다.

### WI-5.009 설치·운영 문서와 라이선스 (0.5)

- `docs/INSTALL.md`, `docs/DEBUGGING.md` 최신화, README 갱신.
- MIT `LICENSE`: 저작권자 이름을 사용자에게 받아 추가한다.

### WI-5.010 인수 기준 최종 점검 (1)

- AC-01~AC-09 결과 표를 `docs/ACCEPTANCE.md`에 남긴다. 실패 항목이 있으면 M3를 완료로 표시하지 않는다.
- 시연 녹화: 부팅 → 모드 전환 → HiDPI → 디스플레이 잠자기·로그인 (`docs/movie/m3-demo.mov`, 자막, 화면 전환 1.5초 / 입력 0.3~0.5초 간격).

> 도구와 수치는 계획 단계의 스케치다. 실측과 사용자 승인이 우선한다.

## 결정 기록

### WI-5.005 보안 경계 점검 (2026-11-25)

- 호스트가 주는 값 16곳(캡 목록·BAR·MMIO offset·큐 수·큐 크기·notify_off·MSI-X 벡터·device_status·used id·used len·응답 type·num_scanouts·해상도·EDID·events_read·ISR)을 모두 확인했다. 모두 쓰기 전에 범위 검사를 받는다. 고칠 곳 없음.
- 그 밖의 입력(boot-args, Info.plist 값, 펌웨어 콘솔 정보, `setProperties`, IOGraphics 호출)도 정규화·검사가 있다.
- user client 없음. `IOUserClient.h`는 `clientHasPrivilege`만 쓴다.
- 표: `docs/research/p5-security-boundary.md`.

### WI-5.006 호환성 점검 (2026-11-25)

- 로드 상태: `as.vit9696.Lilu 1.6.8`, `as.vit9696.WhateverGreen 1.6.7`, VirtioVideo 0.0.41, NdrvBlocker 0.0.2. P2~P4의 모든 시험(모드 전환 10종, HiDPI, 디스플레이 잠자기 10회, 하드웨어 커서)이 이 조합에서 진행됐고 충돌·패닉 없음.
- WhateverGreen이 이 장치를 건드리지 않는 근거:
  - Lilu 1.6.8 `kern_devinfo.cpp`: 장치 목록은 vendor Intel·AMD·AMDZEN·VMware·QEMU(0x1234)·NVIDIA만 받는다. virtio-gpu는 0x1af4라 제외된다. 외부 GPU는 PCI 브리지 아래에서만 찾는데 이 장치는 루트 버스(`/_SB/PCI0/S30`)에 있다.
  - WEG가 외부 GPU에 하는 일(이름 `GFX0`으로 변경, `AAPL,slot-name`, `model`, configRead 후킹)의 흔적이 없다. 장치 이름은 `S30@6`/`display` 그대로다.
  - `"built-in" = <00>` 속성의 출처는 확인하지 못했다. 위 근거로 WEG 경로는 아니다.
- macOS 업데이트: 15.8.3(24H241) 최신. `softwareupdate -l`이 주는 것은 macOS Tahoe 26.7.3뿐이라 범위 밖(Q-05)이므로 시험하지 않는다.

### WI-5.002 모드 전환 100회·누수 시험 (2026-11-25)

- 도구: 새 의존성 없이 `scripts/mode-cycle.swift`(CoreGraphics `CGConfigureDisplayWithDisplayMode`, HiDPI 포함 모드 목록)와 전후 기록용 `scripts/mode-switch-test.sh`.
- 조건: kext 0.0.41, 시작 모드 1280×800 HiDPI(픽셀 2560×1600), 모드 17개를 차례로 100회, 간격 1.5초.
- 결과: 100회 모두 요청한 모드로 바뀜(failures=0), 원래 모드 복원 ok. 패닉 보고서 수 그대로(5, 모두 이전 것). `flush-errors=0`, `timeouts=0`, `device-errors=0`. `VVCurrentMode changes=101`, scanout resource 1 → 103(전환마다 새로 만들고 이전 것은 해제).
- 누수: 드라이버 객체 수 전후 같음(`VirtioGpuDevice 1, VirtioVideoFramebuffer 1, VirtioQueue 2, VVDmaBuffer 6, VirtioPciTransport 1`). 고정 메모리 487,130 → 486,970 페이지(감소). 시스템 전체 `IOBufferMemoryDescriptor`는 446 → 449였지만 모드 전환 없이도 20초 사이 448 → 452로 변하므로 드라이버와 무관하다.
- 관찰: 전환마다 QEMU가 config 변경 이벤트를 하나씩 보낸다(`config=101`). 드라이버는 display info·EDID를 다시 읽고 모드 목록을 맞춘다(WI-3.005 경로).
- `zprint`(root 필요)는 쓰지 않았다. 객체 수와 고정 메모리로 판정했다.

### WI-5.008 상태 조회 CLI (2026-11-25)

- `tools/vvstat.c`(C, IOKit·CoreFoundation, 새 의존성 없음). `scripts/vvstat.sh`가 `build/vvstat`로 빌드해 실행한다.
- 출력: kext 버전(`KextManagerCopyLoadedKextInfo`), 프레임버퍼 연결 여부, 요약 12개(init·mode·preferred·scanout·display power·cursor·dirty flush·edid·events·recoveries·irq·features). `--log N`은 `VVLog` 마지막 N줄, `--all`은 VV* 속성 전체.
- 읽기만 한다: `IORegistryEntryCreateCFProperty(ies)`만 쓰고 user client를 열지 않는다. root 불필요.
- 종료 코드: 0 = `VVInitResult driver-ok`이고 프레임버퍼 연결, 1 = 찾지 못함 또는 동작 중 아님, 2 = 사용법 오류. 실행해 0·2를 확인했다.
- 찾은 결함: `VVEventCounters` 요약이 128바이트를 넘어 `errors`·`recoveries`가 잘렸다(`... timeouts=0 er`). 0.0.42에서 이 요약만 256바이트로 늘렸다. 설치·재부팅 뒤 `vvstat`에서 `... timeouts=0 errors=1 recoveries=0`까지 나오는 것을 확인했다.

### WI-5.007 배경화면·투명 효과 전후 비교 (2026-11-25)

방법: `scripts/ac07-measure.swift`(CoreGraphics 픽셀 읽기, Metal 장치 수)로 §2.5와 같은 위치를 point 단위로 잰다. 이전 캡처에 돌려 §2.5 값(테두리 16 pt RGB(0,124,143), 안쪽 RGB(0,188,217), 메뉴 막대 RGB(218,218,218))이 그대로 나오는 것을 먼저 확인했다. 이후 캡처는 kext 0.0.42, 2560×1600(HiDPI, 1280×800 pt), `screencapture -x`. 배경은 `osascript`로 잠시 바꾸고 원래 값(`Solid Colors/Cyan.png`)으로 되돌렸다.

| 항목 | 적용 전 (2026-11-23, IONDRV 1280×800) | 적용 후 (0.0.42) |
| ---- | ------------------------------------- | ---------------- |
| ① 정적 사진(Sonoma.heic) | 정상, 테두리 없음 | 정상. 가장자리까지 그려짐(왼쪽 RGB(111,150,58), 아래 RGB(179,216,78)), 테두리 없음 — `docs/screenshots/p5/ac07-sonoma-after.png` |
| ② Sequoia 배경 | Cyan 단색 대체 | **여전히 그려지지 않음.** 사용자가 설정에서 Sequoia를 고르니 흰색 단색 RGB(255,255,255)으로 채워짐(대체 색만 Cyan → 흰색). `get picture` = `DefaultDesktop.heic` — `docs/screenshots/p5/ac07-sequoia-after.png` |
| ③ 단색 배경 16 px 테두리 | 있음, RGB(0,124,143) | **없음.** 화면 끝까지 RGB(20,175,208) — 원본 파일 색과 같다 — `docs/screenshots/p5/ac07-cyan-after.png` |
| ④ 메뉴 막대·Dock 투명 효과 | 없음 (RGB 218/200 불투명) | 메뉴 막대 없음: 세 위치 모두 RGB(218,218,218), 뒤 사진이 비치지 않음. Dock 불투명(사용자 확인, 캡처에는 화면 기록 권한이 없어 나오지 않음) |
| ⑤ `MTLCopyAllDevices()` | 빈 목록 | 빈 목록(0개) |

관찰:
- ③ 테두리가 사라지고 안쪽 색이 원본 파일 색과 같아졌다. 배경 저장소에 EDID 기반 디스플레이 UUID(`17E44908-…`, `2043E827-…`, `E368AAE8-…`)가 생겼다(전에는 `FFFFFFFF-…`만). 디스플레이 식별(EDID, WI-3.003)이 테두리·색 문제와 관련 있다는 §2.5의 추정과 맞는다. 인과는 확인하지 못했다.
- ②④⑤는 예상대로 그대로다(Q-12: Metal 범위 밖). Sequoia는 Metal로 그리는 확장이라(§2.5.2) 대체 색만 바뀌었다.

### WI-5.003 언로드·재로드 (2026-11-25)

배경: kext는 이제 OpenCore 주입이 아니라 보조 kernel collection(`/Library/Extensions`, `kmutil load -z -p`)에 있다(Q-01 v1.2). 보조 kext NdrvBlocker는 Boot KC(OpenCore 주입)라 원래 언로드할 수 없다.

| 시험 | 결과 |
| ---- | ---- |
| 시험 전 상태 (01:49:17) | 0.0.42 `driver-ok`, `VirtioGpuDevice 1, VirtioVideoFramebuffer 1, VirtioQueue 2, VVDmaBuffer 6`, 패닉 보고서 5개 |
| 화면 사용 중 `sudo kmutil unload -b local.virtiovideo.VirtioVideo` (사용자가 호스트 스냅샷 후 실행) | **패닉 상태로 멈춤, 자동 재부팅 안 됨.** pmset `01:50:45 Power button pressed for > 4 sec`(강제 종료). 새 패닉 보고서 없음(멈춘 채 저장 못 함), 01:49~01:50 통합 로그도 남지 않음 → 멈춘 위치는 확인 못 함 |

해석: 언로드는 먼저 우리 서비스를 terminate한다. 사용 중인 프레임버퍼를 terminate하면 Apple 쪽 프레임버퍼 클라이언트가 종료 알림을 처리하다 실패한다는 것을 2026-11-24 IONDRV 실험에서 이미 봤다(`AppleUpstreamUserClientDriver::fbNotificationOccurred` NULL 참조, PHASE-PLAN-02). 같은 경로로 보이지만 이번 보고서가 없어 확정하지 못했다.

결론: **화면을 맡고 있는 동안의 언로드·재로드는 지원할 수 없다.** NFR-003을 "kext 교체는 재부팅으로 한다(`kmutil load -z -p` 후 재부팅)"로 바꿨다(사용자 승인 "Change to reboot", REQUIREMENT v1.3). `-vvideooff` 상태의 언로드는 시험하지 않았다.
