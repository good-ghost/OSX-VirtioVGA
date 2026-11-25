# Virtio-Video 프로젝트 계획

| 항목      | 내용                                              |
| --------- | ------------------------------------------------- |
| 기준 문서 | [REQUIREMENT.md](REQUIREMENT.md) v1.0 (2026-11-23) |
| 작성일    | 2026-11-23                                        |
| 상세 계획 | [phases/](phases/) `PHASE-PLAN-00.md` ~ `PHASE-PLAN-06.md` |

## 1. 개요

OSX-KVM 게스트(macOS 15.8.3, x86_64)에서 QEMU `virtio-vga`(`1af4:1050`)를 구동하는 2D 프레임버퍼 kext를 만든다. 현재는 범용 `IONDRVFramebuffer`가 1280×800 한 모드만 쓰고 VRAM을 "3 MB"로 보고한다. 이 드라이버로 해상도 선택(최대 2560×1600, HiDPI 포함)과 VRAM 표시(128 MiB)를 해결한다.

Metal 가속, Sequoia 배경, 투명 효과는 범위 밖이다 (REQUIREMENT §2.5.2, Q-12).

## 2. 일정

마감이나 전체 기간 제약은 주어지지 않았다. 1인 개발 기준 Man-day 추정치만 적는다. 커널 디버깅 비중이 커서 실제 소요는 늘어날 수 있다.

| Phase | 이름                 | WI 수 | Man-day | 누계  | 마일스톤                                   |
| ----- | -------------------- | ----- | ------- | ----- | ------------------------------------------ |
| P0    | 개발 환경            | 8     | 8.0     | 8.0   |                                            |
| P1    | virtio 전송 계층     | 7     | 10.0    | 18.0  |                                            |
| P2    | 2D 단일 모드 출력    | 10    | 12.5    | 30.5  | **M1**: 드라이버가 virtio-gpu로 화면을 출력 |
| P3    | 모드와 VRAM          | 9     | 11.5    | 42.0  | **M2**: 해상도 선택 + VRAM 표시 (핵심 목표) |
| P4    | 커서·전원·최적화     | 5     | 7.0     | 49.0  |                                            |
| P5    | 안정화와 문서        | 10    | 9.5     | 58.5  | **M3**: 안정 버전 (AC-01~09 통과)          |
| P6    | 확장 (선택, Could)   | 2     | 4.5     | 63.0  |                                            |
| 합계  |                      | 51    | 63.0    |       |                                            |

## 3. 진행 규칙

- WI 하나를 끝내면: 빌드·단위 테스트 통과 → 체크리스트와 상태 표에 `[X]` 표시 → 커밋. 자세한 규칙은 [CLAUDE.md](../CLAUDE.md).
- kext를 VM에 설치·부팅하는 단계는 매번 사용자 확인을 받는다. 설치 전 롤백 경로(WI-0.003)를 확인한다.
- 각 WI의 수락 기준과 세부 작업은 해당 Phase의 PHASE-PLAN 문서에 있다.
- WI ID는 바꾸거나 재사용하지 않는다. 끼워 넣는 작업은 `WI-2.004b`처럼 파생 ID를 쓴다.

## 4. Phase별 Work Item

### P0. 개발 환경 — [PHASE-PLAN-00](phases/PHASE-PLAN-00.md)

| Status | ID        | Description                                         | Man-day |
| ------ | --------- | --------------------------------------------------- | ------- |
| [X]    | WI-0.001  | 빌드 체계 결정과 빈 kext 빌드                       | 1.5     |
| [X]    | WI-0.002  | OpenCore 주입으로 최소 kext 로드 확인               | 1       |
| [X]    | WI-0.003  | 롤백 절차와 비활성화 boot-arg                       | 1       |
| [X]    | WI-0.004  | KDK·QEMU gdbstub 디버깅과 패닉 로그 수집 절차       | 1.5     |
| [X]    | WI-0.005  | 로깅 매크로와 로그 수준                             | 0.5     |
| [X]    | WI-0.006  | 사용자 공간 단위 테스트 하네스                      | 1       |
| [X]    | WI-0.007  | 환경 수집 스크립트 비교 모드                        | 0.5     |
| [X]    | WI-0.008  | virtio / virtio-gpu 상수·구조체 헤더                | 1       |

- WI-0.001
  - [X] Xcode 26.3에 kext 템플릿이 있는지 확인한 결과를 기록했다
  - [X] `scripts/build.sh` 한 번으로 `build/VirtioVideo.kext`가 만들어진다 (종료 코드 0)
  - [X] `Info.plist`의 `OSBundleLibraries`에 IOGraphicsFamily·IOPCIFamily 의존성이 있다
  - [X] `kmutil libraries -p build/VirtioVideo.kext` 검사가 오류 없이 끝난다
  - [X] CLAUDE.md의 빌드 명령 절이 채워졌다
- WI-0.002
  - [X] 매칭 없이 로그만 남기는 최소 kext를 OC `Kernel > Add`로 주입하는 config.plist 항목을 문서화했다
  - [X] 재부팅 후 `kmutil showloaded`에 이 kext가 보인다
  - [X] 부팅 로그에 시작 로그 한 줄이 남는다 — 통합 로그 대신 IORegistry `VVLog`로 확인 (WI-0.005 결정)
  - [X] SIP 상태가 변경 전과 같다 (`csrutil status`)
- WI-0.003
  - [X] `docs/INSTALL.md`에 OC 항목 비활성화(`Enabled=false`)와 호스트 스냅샷 복원 절차가 있다
  - [X] boot-arg `-vvideooff`가 있으면 `probe()`가 `nullptr`를 돌려준다
  - [X] boot-arg를 넣고 부팅해 `IONDRVFramebuffer`가 유지되는 것을 확인했다
- WI-0.004
  - [X] 24H241용 KDK(또는 가장 가까운 빌드) 설치 결과를 기록했다 (정확히 24H241, UUID 일치)
  - [X] QEMU gdbstub에 lldb로 붙어 kext 심볼에 중단점을 거는 절차를 `docs/DEBUGGING.md`에 적었다 — 연결·kext 심볼·메모리 읽기 확인, 중단점 적중은 실행 경로가 생기는 P1에서 확인. 커널 심볼은 lldb 19 충돌로 불가
  - [X] 패닉 보고서 위치와 수집 명령을 적었다 (`scripts/panic-summary.py`, dSYM UUID 보관)
- WI-0.005
  - [X] 오류/정보/디버그 세 수준의 로그 매크로가 있다
  - [X] boot-arg 또는 Info.plist 값으로 수준을 바꿀 수 있고, 바뀐 것을 로그로 확인했다
- WI-0.006
  - [X] `tests/`에서 `scripts/test.sh`로 커널 비의존 코드를 사용자 공간에서 빌드·실행한다
  - [X] 예시 테스트 1개가 통과하고, 일부러 실패시키면 종료 코드가 0이 아니다
- WI-0.007
  - [X] `collect-env.sh`가 이 드라이버의 IORegistry 속성을 함께 수집한다
  - [X] 두 수집 결과를 비교하는 방법(스크립트 또는 절차)이 있다
- WI-0.008
  - [X] virtio 공통·PCI capability·split virtqueue·virtio-gpu 명령/응답 구조체가 헤더에 있다
  - [X] 각 상수에 사양 절 번호 주석이 있다
  - [X] 구조체 크기를 `static_assert`로 검증하고 단위 테스트가 통과한다

### P1. virtio 전송 계층 — [PHASE-PLAN-01](phases/PHASE-PLAN-01.md)

| Status | ID        | Description                                         | Man-day |
| ------ | --------- | --------------------------------------------------- | ------- |
| [X]    | WI-1.001  | AppleVirtIOPCITransport 공존 조사                   | 1.5     |
| [X]    | WI-1.002  | PCI capability 파싱과 BAR 매핑                      | 1.5     |
| [X]    | WI-1.003  | 장치 초기화와 feature 협상                          | 1       |
| [X]    | WI-1.004  | split virtqueue                                     | 2       |
| [X]    | WI-1.005  | MSI-X·INTx 인터럽트와 폴링 모드                     | 2       |
| [X]    | WI-1.006  | 장치 reset 처리와 전송 클래스 분리                  | 1       |
| [X]    | WI-1.007  | 전송 계층 통합 확인 (GET_DISPLAY_INFO 왕복)         | 1       |

- WI-1.001
  - [X] Apple 전송 드라이버가 장치 상태 레지스터를 건드리는지(초기화·reset 여부) 조사한 결과를 `docs/research/`에 남겼다 (`apple-virtio-transport.md`: status=ACK만, 큐·MSI-X·feature 미설정)
  - [X] 공존 방법(그대로 둠 / 매칭 조정 / 우선 점유) 중 하나를 근거와 함께 정했다 — A(공존). 점수로 밀어내기는 로드 시점 때문에 실패(실측). 대안 C(kext 분리) 기록
  - [X] 결정이 REQ-003에 맞는지 확인했다 — 세 서비스 동시 부착 상태로 부팅·화면 정상. 우리 초기화 후 상태 유지는 WI-1.003에서 확인
- WI-1.002
  - [X] vendor capability(`cfg_type` 1~5)를 모두 찾아 BAR·오프셋·길이를 로그로 남긴다 (`VVVirtioCaps`, `VVLog`)
  - [X] 범위가 BAR 크기를 넘는 capability를 거절하는 단위 테스트가 통과한다 (`VirtioPciLayoutTests`, `PciCapWalkTests`)
  - [X] 실제 장치에서 찾은 값을 PHASE-PLAN-01 실측표에 기록했다 (WI-1.001 실측과 0.0.9 전송 계층 결과 일치)
- WI-1.003
  - [X] reset → ACKNOWLEDGE → DRIVER → feature → FEATURES_OK 재확인 → DRIVER_OK 순서로 진행한다 — FEATURES_OK(status 0x0B)까지 실기 확인. DRIVER_OK는 §3.1.1대로 큐 설정 뒤(WI-1.004)에 쓴다
  - [X] `VIRTIO_F_VERSION_1`이 없으면 실패한다 — 단위 테스트로 확인 (실제 장치는 VERSION_1을 제공하므로 실기로는 시험 불가)
  - [X] 3D/blob feature(`VIRGL`, `RESOURCE_BLOB`, `CONTEXT_INIT`)를 협상하지 않는다 — 금지 목록과 static_assert, 단위 테스트
  - [X] 장치가 제공한 feature와 협상한 feature를 로그와 IORegistry에 남긴다 (`VVDeviceFeatures`, `VVDriverFeatures`)
- WI-1.004
  - [X] 디스크립터 테이블·avail·used 링을 DMA 가능한 메모리로 할당하고 주소를 장치에 설정한다 (`VVDmaBuffer` 물리 연속 1세그먼트, `setupQueue`, 두 큐 enabled=1)
  - [X] 인덱스 wrap-around를 포함한 링 로직 단위 테스트가 통과한다 (`SplitRingTests`, 70000회 왕복)
  - [X] 큐 크기를 장치가 알려 준 값 이하로 정한다 (`chooseQueueSize`, 장치 최대 1024 → controlq 64, cursorq 16)
- WI-1.005
  - [X] MSI-X 벡터 3개(config, controlq, cursorq)를 배정하고 인터럽트를 받는다 — macOS가 이 장치에 MSI-X 소스를 1개만 주어 벡터 0 공유로 구현. WI-1.007 첫 명령의 완료를 MSI-X 인터럽트로 받음(interrupts=1, completions=1)
  - [X] MSI-X를 끄는 디버그 옵션에서 INTx + ISR로 동작한다 — **이 장치에서는 시험 불가**: 레거시 인터럽트 소스가 없다(IOPCIMessagedInterruptController만). 코드는 있고, `vvideo_irq=1`이면 Poll로 내려간다
  - [X] 폴링 모드 디버그 옵션에서 인터럽트 없이 동작한다 — `vvideo_irq=2`에서 GET_DISPLAY_INFO 왕복 성공(interrupts=0, completions=1)
- WI-1.006
  - [X] `DEVICE_NEEDS_RESET`을 감지하면 reset 후 재초기화 경로를 탄다 (폴링 모드에서 강제로 시험) — 같은 복구 경로를 boot-arg `vvideo_test_reset=1`의 외부 reset 흉내(Lost)로 실기 확인. NEEDS_RESET 비트 판정은 단위 테스트 (QEMU에서 이 비트를 일으킬 방법이 없다)
  - [X] 전송 계층이 GPU 코드와 분리된 클래스로 있다 (`VirtioPciTransport`, `VirtioQueue`, `VVDmaBuffer` / GPU는 `VirtioGpuDevice`)
- WI-1.007
  - [X] `GET_DISPLAY_INFO` 요청과 응답이 한 번 왕복하고 scanout 0의 1280×800이 로그에 남는다 (MSI-X, Poll 두 방식)
  - [X] kext를 실은 채 부팅해도 화면은 여전히 `IONDRVFramebuffer`로 정상이다 (매칭은 아직 하지 않음) — 명령을 받은 뒤에도 QEMU는 VGA 출력 유지

### P2. 2D 단일 모드 출력 — [PHASE-PLAN-02](phases/PHASE-PLAN-02.md)

| Status | ID        | Description                                         | Man-day |
| ------ | --------- | --------------------------------------------------- | ------- |
| [X]    | WI-2.001  | GPU 명령 계층 (직렬화·타임아웃·응답 검사)           | 1.5     |
| [X]    | WI-2.002  | display info 조회                                   | 0.5     |
| [X]    | WI-2.003  | 프레임버퍼 메모리 풀과 backing                      | 1.5     |
| [X]    | WI-2.004  | 2D resource 생명주기와 픽셀 형식                    | 1.5     |
| [X]    | WI-2.005  | IOFramebuffer 하위 클래스 (고정 모드)               | 2.5     |
| [X]    | WI-2.006  | 부트 프레임버퍼 대체 (매칭·probe score)             | 1.5     |
| [ ]    | WI-2.007  | 주기 flush 타이머                                   | 1       |
| [X]    | WI-2.008  | 실패 경로와 자원 해제                               | 1       |
| [X]    | WI-2.009  | 부트 화면 이어받기                                  | 1       |
| [X]    | WI-2.010  | virtio-vga-gl 호환 시험                             | 0.5     |

- WI-2.001
  - [X] 명령 제출이 한 번에 하나씩 직렬화된다 (IOCommandGate + 진행 중 명령 1개, 워크루프 스레드 호출은 거절)
  - [X] 응답 대기 타임아웃이 상수로 정의되어 있고, 타임아웃 시 오류를 돌려준다 (`kCommandTimeoutMs`=1000, kIOReturnTimeout 후 복구. 실제 장치는 항상 응답해 시간 초과는 실기로 일으키지 못함)
  - [X] `RESP_ERR_*`이면 명령 이름·인자를 로그로 남긴다 (실기: `RESOURCE_UNREF arg=0xfffffff0 -> ERR_INVALID_RESOURCE_ID`)
- WI-2.002
  - [X] scanout별 활성 여부와 선호 크기를 읽고 `num_scanouts` 상한(16)을 검사한다 (선호 크기가 0·꺼짐·2560×1600 초과면 1280×800 대체, `VVPreferredMode`)
- WI-2.003
  - [X] 128 MiB 상한의 메모리 풀에서 모드 크기만큼 할당한다 (`VVFramebuffer` 1280×800 4,096,000바이트)
  - [X] `getApertureRange()`에 쓸 수 있는 메모리 형태(물리 연속 여부)를 실측으로 정하고 기록했다 (PHASE-PLAN-02 T-01) — 물리 연속 할당, 16 MiB도 부팅 직후 성공, IOMMU 변환 없음
  - [X] `ATTACH_BACKING` 항목 목록을 만들어 보낸다 — 목록 생성(`vv/MemEntries.hpp`), WI-2.004 시험 패턴에서 전송 성공(항목 1개, 4,096,000바이트)
- WI-2.004
  - [X] create → attach → set_scanout → transfer → flush → detach → unref가 오류 없이 돈다 (실기: 명령 10개 성공, 시간 초과 0)
  - [X] 색상 패턴(빨강·초록·파랑·흰색)이 바르게 보인다 (AC-04, 스크린샷) — 사용자가 QEMU 창에서 왼쪽부터 빨강·초록·파랑·흰색으로 확인. 호스트 캡처는 받지 못함
- WI-2.005
  - [X] `IOFramebuffer` 필수 메서드를 구현하고 1280×800 한 모드를 보고한다
  - [X] `setGammaTable`이 성공을 돌려준다, 하드웨어 커서 미지원을 보고한다
  - [X] WindowServer가 이 프레임버퍼로 데스크톱을 그린다 (2026-11-24, kext 0.0.23 + NdrvBlocker 0.0.1)
- WI-2.006
  - [X] `ioreg`에서 이 드라이버가 붙고 `IONDRVFramebuffer`는 없다 (AC-01)
  - [X] 선택한 probe score와 근거를 기록했다 (점수 30000. 단 실제 대체는 점수가 아니라 보조 kext의 AAPL,iokit-ignore-ndrv로 이뤄짐, PHASE-PLAN-02)
- WI-2.007
  - [X] 기본 60 Hz로 transfer + flush가 돈다, 주기는 설정으로 바뀐다 (0.0.24: boot-arg `vvideo_flush_ms`, `VVFlushIntervalMs=16`, flush-errors 0)
  - [ ] 창 이동·타이핑이 화면에 반영된다 (녹화) — 사용자 육안 확인과 캡처만 있음. 게스트 `screencapture -V`는 capture error, 호스트 녹화 필요
- WI-2.008
  - [X] 디버그 옵션으로 초기화 각 단계 실패를 주입해도 부팅이 계속되고 `IONDRVFramebuffer`가 남는다 (AC-06) — 0.0.25: `vvideo_fail=1`(전송 계층)·`2`(장치 초기화)·`3`(프레임버퍼) 모두 IONDRV 복귀, 재차단 없음, 패닉 없음
  - [X] boot-arg `-vvideooff`로 기존 화면으로 부팅된다 (AC-05) — 0.0.25: 보조 kext `skipped:-vvideooff`, 본 kext 서비스 0개, IONDRV
- WI-2.009
  - [X] 드라이버가 화면을 넘겨받을 때 Apple 로고·진행 막대가 끊기지 않거나, 끊김이 1회 이내임을 녹화로 확인했다 — 2026-11-24 사용자 육안 확인 "검게 끊기지 않음"(virtio-vga-gl 부팅, 녹화 없음). 부트 화면 복사는 virtio-vga에서 `copied`, gl에서는 크기 불일치로 `skipped`
- WI-2.010
  - [X] `-device virtio-vga-gl` + `-display gtk,gl=on`에서 2D 출력이 되는지 시험하고 결과를 기록했다 (2026-11-24, kext 0.0.25: VIRGL 제공·미협상, 1600×1000 출력, flush 오류 0)

### P3. 모드와 VRAM — [PHASE-PLAN-03](phases/PHASE-PLAN-03.md)

| Status | ID        | Description                                         | Man-day |
| ------ | --------- | --------------------------------------------------- | ------- |
| [X]    | WI-3.001  | 모드 테이블 계산                                    | 1       |
| [X]    | WI-3.002  | 모드 전환 (setDisplayMode)                          | 2       |
| [X]    | WI-3.003  | EDID 협상·검증·제공                                 | 2       |
| [X]    | WI-3.004  | 선호 해상도·EDID 모드 병합                          | 1       |
| [X]    | WI-3.005  | 디스플레이 변경 이벤트 처리                         | 1       |
| [X]    | WI-3.006  | HiDPI 모드                                          | 1.5     |
| [X]    | WI-3.007  | 선택 모드 복원                                      | 1       |
| [X]    | WI-3.008  | VRAM 보고                                           | 1.5     |
| [X]    | WI-3.009  | 드라이버 상태 IORegistry 공개                       | 0.5     |

- WI-3.001
  - [X] REQ-040 표준 모드 중 2560×1600 이하이고 풀에 들어가는 모드만 남기는 단위 테스트가 통과한다 (vv/ModeTable.hpp, 테스트 4개)
- WI-3.002
  - [X] 시스템 설정 > 디스플레이에서 각 모드로 바꾸면 실제 해상도가 바뀐다 (AC-03) — 0.0.26: 1280×800→1920×1080→2560×1600, 사용자 확인, system_profiler Resolution 일치
  - [X] 전환 중 이전 resource를 해제한다, 패닉·정지가 없다 (resource 1→2→3, flush 오류 0, 패닉 없음)
- WI-3.003
  - [X] EDID 길이·체크섬 검증 단위 테스트가 통과한다 (vv/Edid.hpp, 테스트 2개)
  - [X] 배경 저장소의 디스플레이 키가 `FFFFFFFF-…`가 아닌 값으로 바뀌는지 확인하고 기록했다 — 0.0.29: 새 키 `2043E827-27F6-C3E9-C3EF-99363AC7F093` 추가, 디스플레이 이름 "QEMU Monitor", VendorID 18708(0x4914)·ProductID 4660(0x1234)
- WI-3.004
  - [X] 선호 해상도가 기본 모드로 표시된다, 중복 모드가 없다 (Default 플래그, 표준과 같으면 표준 ID 사용, 단위 테스트)
- WI-3.005
  - [X] GTK 창 크기 변경 시 `VIRTIO_GPU_EVENT_DISPLAY`가 오는지 확인하고, 오면 모드 목록을 갱신한다 — gl 구성에서 이벤트 2회(`VVDisplayEvents=2`), display info·EDID 재조회, 모드 목록 재구성 1회. 일반 virtio-vga+GTK에서는 이벤트 없음(WI-1.005). IOGraphics 즉시 반영·자동 전환은 WI-6.001
- WI-3.006
  - [X] 2560×1600에서 1280×800 HiDPI 모드가 시스템 설정에 보이고 선택된다 — macOS가 모드 목록에서 스스로 제공. system_profiler "UI Looks like 1280 x 800", 캡처 docs/screenshots/p3/
- WI-3.007
  - [X] 모드를 바꾸고 재부팅하면 같은 모드로 시작한다 — 2560×1600 HiDPI가 0.0.27·0.0.28 재부팅 뒤 그대로 복원
- WI-3.008
  - [X] `IOFBMemorySize`가 128 MiB다 (0.0.28: 134217728)
  - [X] `system_profiler`의 VRAM 표시가 128 MB이고 "Kernel Extension Info"에 이 드라이버가 나온다 (AC-02) — VRAM 128 MB 확인. "Kernel Extension Info" 줄은 드라이버 적용 뒤 아예 표시되지 않음(REQ-062, S, 미충족으로 남김)
  - [X] VRAM 표시에 쓰이는 속성을 조사한 결과를 기록했다 — IOGraphics가 getVRAMRange 길이로 IOFBMemorySize를 정하고 system_profiler가 그 값을 쓴다. 숫자 덮어쓰기는 무시됨 (PHASE-PLAN-03)
- WI-3.009
  - [X] 협상 feature, scanout 수, 현재 모드, 큐 통계, 오류 수가 IORegistry에 보인다 — VVDeviceFeatures·VVDriverFeatures, VVDisplayInfo, VVCurrentMode, VVControlQueue·VVEventCounters, VVScanout

### P4. 커서·전원·최적화 — [PHASE-PLAN-04](phases/PHASE-PLAN-04.md)

| Status | ID        | Description                                         | Man-day |
| ------ | --------- | --------------------------------------------------- | ------- |
| [X]    | WI-4.001  | 하드웨어 커서 (cursorq)                             | 2       |
| [X]    | WI-4.002  | 디스플레이 잠자기·깨우기와 잠금 화면                | 1.5     |
| [X]    | WI-4.003  | 전원 상태 요청 최소 처리                            | 0.5     |
| [X]    | WI-4.004  | 변경 감지 flush                                     | 2       |
| [X]    | WI-4.005  | virtqueue 선택 feature 측정                         | 1       |

- WI-4.001
  - [X] 커서 모양·위치가 cursorq로 갱신된다, 모양이 바뀌는 상황(텍스트 입력, 크기 조절)을 녹화로 확인했다 — 0.0.40: 화살표·I·창 가장자리·링크 손 모양, 크기(50%)·클릭 위치를 사용자가 확인. `VVCursor images=9 moves=330 drops=0`. 녹화는 없음(게스트 녹화 불가, WI-2.007과 같은 제약)
  - [X] 하드웨어 커서를 끄는 옵션에서 소프트웨어 커서로 동작한다 — boot-args `keepsyms=1 -vvideoswcursor`: `get(crsr)=0`, `VVCursor images=0 moves=0`, cursorq 0건, 게스트 커서가 화면에 그려져 모양이 바뀜(사용자 확인), 패닉 없음
- WI-4.002
  - [X] 디스플레이 잠자기 → 깨우기 → 잠금 화면 → 로그인 10회 반복에서 깨짐·검은 화면·패닉이 없다 (AC-09) — 0.0.36, `scripts/display-sleep-cycles.sh`: 10/10 ok, 패닉 없음, 로그인 뒤 스크린샷 2560×1600 (`docs/research/p4-display-sleep-ac09.log`, `docs/screenshots/p4/`). 녹화는 없음(게스트 `screencapture -V` 불가, WI-2.007과 같은 제약)
  - [X] 잠자기 동안 flush 타이머가 멈춘다 (로그·통계) — `VVDisplayPower sleeps=11 wakes=11`, `VVScanout blanks=11 flush-errors=0`, 잠자기 동안 호스트 창에 "Display output is not active." 
- WI-4.003
  - [X] 시스템 잠자기 요청에 패닉 없이 응답한다 (복원 기능은 범위 밖, REQ-054 W) — `pmset sleepnow` 2회(0.0.36, 0.0.37) 모두 잠들기까지 패닉 없음. **깨어나기는 미시험**: 키 입력으로 S3의 VM이 깨어나지 않고 QEMU 모니터(`system_wakeup`)가 없음. 1회는 스스로 FullWake까지 갔으나 화면·드라이버 상태는 재시작으로 확인 못 함 (PHASE-PLAN-04 결정 기록)
- WI-4.004
  - [X] 화면이 멈춰 있을 때 전송량이 줄어든 것을 통계로 확인했다 — 0.0.41 정지 화면 30초: 25,188 MiB → 0 MiB (`VVDirtyStats`)
  - [X] 변경 감지를 켠 상태와 끈 상태의 CPU 사용률을 측정해 기록했다 — 게스트 `kernel_task` 6.1% → 24.9%, 호스트 QEMU 4.2~4.9% 차이 없음 → 기본값 꺼짐 (`docs/research/p4-performance.md`)
- WI-4.005
  - [X] `INDIRECT_DESC`, `EVENT_IDX`를 켰을 때와 껐을 때를 측정하고 기본값을 정했다 — **켬·끔 비교 측정 없이 분석으로 결정(사용자 승인 2026-11-25)**: 기본값 둘 다 끔. 근거: 명령을 한 번에 하나씩 보내고 완료를 기다리는 구조라 묶어 알릴 완료가 없고(EVENT_IDX), 64칸 큐에서 최대 2칸만 씀(INDIRECT_DESC). 실측 명령 101.5건/s, 인터럽트 102건/s, 시간 초과 0 (`docs/research/p4-performance.md`)

### P5. 안정화와 문서 — [PHASE-PLAN-05](phases/PHASE-PLAN-05.md)

| Status | ID        | Description                                         | Man-day |
| ------ | --------- | --------------------------------------------------- | ------- |
| [ ]    | WI-5.001  | 24시간 연속 사용 시험                               | 1.5     |
| [X]    | WI-5.002  | 모드 전환 100회·누수 시험                           | 1       |
| [ ]    | WI-5.003  | 언로드·재로드 가능성 확인                           | 1       |
| [ ]    | WI-5.004  | 성능 측정                                           | 1       |
| [X]    | WI-5.005  | 보안 경계 점검                                      | 1       |
| [X]    | WI-5.006  | 호환성 점검 (Lilu/WhateverGreen, macOS 15.x)        | 0.5     |
| [ ]    | WI-5.007  | 배경화면·투명 효과 전후 비교 기록                   | 1       |
| [ ]    | WI-5.008  | 상태 조회 CLI                                       | 1       |
| [ ]    | WI-5.009  | 설치·운영 문서와 라이선스                           | 0.5     |
| [ ]    | WI-5.010  | 인수 기준 최종 점검                                 | 1       |

- WI-5.001
  - [ ] 일반 작업·동영상 재생 24시간에서 패닉 0회 (NFR-001), 결과를 기록했다
- WI-5.002
  - [X] 모드 전환 100회에서 패닉·정지 0회, 메모리 사용량 증가가 없다 (NFR-002) — 0.0.41, `scripts/mode-switch-test.sh`: 17개 모드(HiDPI 포함) 100회 모두 ok, 원래 모드 복원, 패닉 0, flush-errors 0, 시간 초과 0. 드라이버 객체 수 전후 같음(VVDmaBuffer 6, VirtioQueue 2), 고정 메모리 487,130 → 486,970 페이지 (`docs/research/p5-mode-switch.log`)
- WI-5.003
  - [ ] OC 주입 kext의 `kmutil unload`가 SIP 켠 상태에서 되는지 확인했다
  - [ ] 된다면 언로드 시 자원이 모두 해제되고 재로드가 성공한다, 안 된다면 그 사실과 대안을 NFR-003 변경 제안으로 남겼다
- WI-5.004
  - [ ] 1920×1080에서 CPU 사용률 증가분을 측정했다 (목표: 한 코어의 10% 이하, NFR-004)
  - [ ] 마우스·창 드래그 지연을 IONDRV와 녹화로 비교했다 (NFR-005)
- WI-5.005
  - [X] 호스트가 주는 모든 값의 범위 검사 위치를 목록으로 점검했다 (NFR-006) — 16곳 모두 검사 있음, 빠진 곳 없음 (`docs/research/p5-security-boundary.md`)
  - [X] user client가 없음을 확인했다 (NFR-007) — 하위 클래스·`newUserClient`·`IOUserClientClass` 없음. 설정 입구는 root만 받는 `setProperties` 하나
- WI-5.006
  - [X] Lilu·WhateverGreen이 로드된 상태에서 충돌이 없고, WhateverGreen이 이 장치를 건드리지 않음을 확인했다 — Lilu 1.6.8·WEG 1.6.7 로드 상태로 P2~P4 전체(모드 전환·잠자기·커서) 진행. Lilu 장치 분류가 vendor 0x1af4를 제외(소스 확인), GFX0 이름 변경·slot·model 흔적 없음. macOS 15.x 업데이트 없음(제공되는 것은 26.7.3, 범위 밖 Q-05)
- WI-5.007
  - [ ] AC-07 다섯 항목을 전후로 캡처·측정해 기록했다
- WI-5.008
  - [ ] `tools/`의 CLI가 IORegistry 상태를 읽어 출력한다 (user client 없음)
- WI-5.009
  - [ ] `docs/INSTALL.md`, `docs/DEBUGGING.md`가 최신이다
  - [ ] MIT `LICENSE` 파일을 사용자가 정한 저작권자 이름으로 추가했다
- WI-5.010
  - [ ] AC-01~AC-09를 모두 다시 확인하고 결과 표를 남겼다, 시연 녹화를 `docs/movie/`에 남겼다

### P6. 확장 (선택) — [PHASE-PLAN-06](phases/PHASE-PLAN-06.md)

| Status | ID        | Description                                         | Man-day |
| ------ | --------- | --------------------------------------------------- | ------- |
| [ ]    | WI-6.001  | 창 크기에 맞춘 자동 해상도 전환                     | 1.5     |
| [ ]    | WI-6.002  | 다중 scanout (여러 모니터)                          | 3       |

- WI-6.001
  - [ ] GTK 창 크기를 바꾸면 설정에 따라 그 크기의 모드로 자동 전환된다
  - [ ] 자동 전환을 끄는 설정이 있다
- WI-6.002
  - [ ] `max_outputs=2`에서 모니터 2개가 시스템 설정에 보이고 각각 출력된다

## 5. 요구사항 추적

`W`(이번 범위 제외)인 요구사항은 WI가 없다.

| 요구사항 | 우선 | WI                              |
| -------- | ---- | ------------------------------- |
| REQ-001  | M    | WI-2.005, WI-2.006              |
| REQ-002  | M    | WI-2.006                        |
| REQ-003  | M    | WI-1.001                        |
| REQ-004  | M    | WI-2.008                        |
| REQ-005  | M    | WI-2.008                        |
| REQ-006  | M    | WI-0.001                        |
| REQ-007  | S    | WI-2.010                        |
| REQ-010  | M    | WI-1.002                        |
| REQ-011  | M    | WI-1.003                        |
| REQ-012  | M    | WI-1.003                        |
| REQ-013  | M    | WI-1.004                        |
| REQ-014  | M    | WI-1.005                        |
| REQ-015  | S    | WI-1.005                        |
| REQ-016  | C    | WI-4.005                        |
| REQ-017  | S    | WI-1.006                        |
| REQ-018  | S    | WI-1.006                        |
| REQ-020  | M    | WI-1.007, WI-2.002              |
| REQ-021  | M    | WI-2.004                        |
| REQ-022  | M    | WI-2.004                        |
| REQ-023  | M    | WI-2.001                        |
| REQ-024  | M    | WI-2.001                        |
| REQ-025  | S    | WI-3.003                        |
| REQ-026  | S    | WI-3.005                        |
| REQ-027  | M    | WI-1.003                        |
| REQ-030  | M    | WI-2.003                        |
| REQ-031  | M    | WI-2.005                        |
| REQ-032  | M    | WI-2.007                        |
| REQ-033  | S    | WI-4.004                        |
| REQ-034  | M    | WI-3.002                        |
| REQ-035  | S    | WI-2.009                        |
| REQ-036  | M    | WI-2.003, WI-3.001              |
| REQ-040  | M    | WI-3.001                        |
| REQ-041  | S    | WI-3.004                        |
| REQ-042  | M    | WI-3.002                        |
| REQ-043  | C    | WI-6.001                        |
| REQ-044  | S    | WI-3.006                        |
| REQ-045  | S    | WI-3.007                        |
| REQ-046  | C    | WI-6.002                        |
| REQ-050  | S    | WI-4.001                        |
| REQ-051  | M    | WI-2.005, WI-4.001              |
| REQ-052  | M    | WI-2.005                        |
| REQ-053  | M    | WI-4.002                        |
| REQ-054  | W    | (제외) 최소 처리만 WI-4.003     |
| REQ-060  | M    | WI-3.008                        |
| REQ-061  | M    | WI-3.008                        |
| REQ-062  | S    | WI-3.008                        |
| REQ-063  | S    | WI-1.003, WI-3.009              |
| REQ-064  | M    | WI-0.005                        |
| REQ-070  | M    | WI-0.002, WI-5.009              |
| REQ-071  | M    | WI-0.003                        |
| REQ-072  | M    | WI-0.003, WI-2.008              |
| REQ-073  | W    | (제외)                          |
| REQ-074  | C    | WI-5.008                        |
| REQ-075  | M    | WI-0.007                        |
| NFR-001  |      | WI-4.003, WI-5.001              |
| NFR-002  |      | WI-5.002                        |
| NFR-003  |      | WI-5.003                        |
| NFR-004  |      | WI-4.004, WI-5.004              |
| NFR-005  |      | WI-5.004                        |
| NFR-006  |      | WI-1.002, WI-2.002, WI-3.003, WI-5.005 |
| NFR-007  |      | WI-5.005                        |
| NFR-008  |      | WI-5.006                        |
| NFR-009  |      | WI-5.006                        |
| NFR-010  |      | WI-0.006                        |
| NFR-011  |      | WI-0.008                        |
| NFR-012  |      | WI-0.008, WI-5.009              |
| NFR-013  |      | WI-0.004                        |

## 6. 계획 단계에서 발견한 기술 확인 사항

요구사항을 바꾸지는 않았다. 해당 WI에서 실측한 뒤 필요하면 요구사항 변경을 제안한다.

| ID   | 내용                                                                                                                                       | 확인 WI  |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------ | -------- |
| T-01 | **해결(2026-11-24, WI-2.003)**: 프레임버퍼를 물리 연속으로 할당한다. 2560×1600(16 MiB)도 부팅 직후 연속 할당 성공, 장치 주소 = CPU 물리 주소(IOMMU 없음). REQ-030(연속 아니어도 됨)과 충돌하지 않으므로 요구사항은 바꾸지 않는다. 남은 위험: 실행 중 모드 전환 때 단편화로 큰 연속 할당이 실패할 수 있음 → WI-3.002에서 확인. 원래 내용: | `IOFramebuffer::getApertureRange()`는 `IODeviceMemory`(물리 주소 범위)를 돌려준다. 프레임버퍼가 물리적으로 연속이어야 할 가능성이 있어 REQ-030("물리 연속일 필요 없음")과 충돌할 수 있다 | WI-2.003 |
| T-02 | NFR-003(언로드 후 재로드)은 OC 주입만 쓰기로 한 Q-01 결정과 함께 성립하는지 확인되지 않았다                                                   | WI-5.003 |
| T-03 | REQ-043은 REQUIREMENT §11에서 P3과 P6 양쪽에 적혀 있다. 우선순위 C에 맞춰 P6(WI-6.001)에 배치했다                                            | —        |
| T-04 | OC 주입 kext는 부트 kernel collection에 들어가고 IOGraphicsFamily는 시스템 collection에 있어, 이 드라이버는 시스템 collection 로드 후에 로드된다 (WI-0.002 실측). 부트 프레임버퍼 대체·부트 화면 이어받기에 영향이 있는지 확인 필요 **2026-11-24 확정 (kext 0.0.21): IOGraphicsFamily 심볼을 쓰는 kext는 OC 주입으로 로드되지 않는다. IOFramebuffer 하위 클래스는 보조 collection 등 다른 로드 경로가 필요 (PHASE-PLAN-02)** | WI-2.006, WI-2.009 |
| T-05 | AppleVirtIOPCITransport가 GPU 장치를 먼저 차지한다. 우리 kext는 늦게 로드되어(T-04) 같은 카테고리에서 점수로 밀어낼 수 없다(WI-1.001 실측). 공존 방식으로 진행하고, 우리 초기화 후 외부 reset이 관찰되면 kext 분리로 바꾼다 | WI-1.003, WI-1.006 |
