# PHASE-PLAN-04 — 커서·전원·최적화

| 항목      | 내용                                                  |
| --------- | ----------------------------------------------------- |
| Phase     | P4                                                    |
| 기간      | 7.0 Man-day                                           |
| 관련 요구 | REQ-016, 033, 050, 051, 053, 054(최소), NFR-001, 004  |
| 선행      | P3 완료 (M2)                                          |
| 완료 조건 | 하드웨어 커서, 디스플레이 잠자기·잠금 화면 반복(AC-09), flush 부하 측정 결과가 있다 |

## 1. 목표

일상 사용 품질을 맞춘다. 사용자 환경은 시스템 잠자기를 쓰지 않고 **10분 뒤 디스플레이 잠자기 → 즉시 잠금**으로 동작한다 (REQUIREMENT §8.1.3). 따라서 디스플레이 잠자기·깨우기는 필수(M)이고, 시스템 잠자기 복원은 범위 밖이다.

## 2. 의존 관계와 실행 순서

```
WI-4.002 ─> WI-4.003
WI-4.001 (독립)
WI-4.004 ─> WI-4.005
```

WI-4.002를 먼저 한다. 사용자가 매일 겪는 동작이기 때문이다.

## 3. Work Item 상세

### WI-4.001 하드웨어 커서 (2)

- `getAttribute(kIOHardwareCursorAttribute)`에서 지원 보고, `setCursorImage`로 받은 이미지를 64×64 ARGB 커서 resource로 만들어 `UPDATE_CURSOR`, `setCursorState`는 `MOVE_CURSOR`.
- 사양상 `UPDATE_CURSOR`/`MOVE_CURSOR`도 응답(`RESP_OK_NODATA`)이 온다 (WI-0.008 확인). 커서 이동은 자주 일어나므로 응답을 동기로 기다리지 않고 used 링에서 회수만 한다.
- 커서 이미지 형식 변환(IOGraphics가 넘기는 형식 → ARGB)은 순수 함수로 단위 테스트.
- 옵션으로 끄면 소프트웨어 커서 (REQ-051).

### WI-4.002 디스플레이 잠자기·깨우기와 잠금 화면 (1.5)

- IOFramebuffer 전원 상태 변경 시 flush 타이머 정지/재개, 깨울 때 scanout 재설정과 전체 flush.
- 시험(AC-09): `pmset displaysleepnow`로 잠자기 → 키 입력으로 깨우기 → 잠금 화면 → 로그인, 10회. 매회 스크린샷 크기·내용 확인.
- 녹화: `docs/movie/p4-display-sleep.mov` (자막).

### WI-4.003 전원 상태 요청 최소 처리 (0.5)

- 시스템 잠자기 요청에 패닉 없이 응답만 한다(복원 범위 밖, REQ-054 W).
- 사용자 환경은 `pmset sleep 1`로 설정돼 있다 (R-09). 시험 전에 실제로 잠들 수 있는지 사용자와 확인하고, 잠들면 최소한 패닉이 없는지 본다.

### WI-4.004 변경 감지 flush (2)

- 화면을 타일(예: 64×64)로 나누고, 직전 전송본과 비교 또는 해시로 바뀐 타일만 `TRANSFER_TO_HOST_2D`(사각형 지정) → 합친 영역 `RESOURCE_FLUSH`.
- 비교에 드는 CPU와 전송 절감을 측정해 기본값(켜기/끄기, 타일 크기)을 정한다.
- 타일 계산·사각형 병합은 순수 함수로 단위 테스트.
- 측정: 1920×1080, 정지 화면/타이핑/동영상 세 경우의 CPU 사용률(`top`, `powermetrics`는 sudo 필요 → 사용자 실행).

### WI-4.005 virtqueue 선택 feature 측정 (1)

- `INDIRECT_DESC`, `EVENT_IDX` 켬/끔 조합에서 명령 처리량·CPU를 측정해 기본값을 정한다 (REQ-016, C).

## 4. 기록

- 측정 결과는 `docs/research/p4-performance.md`에 표로 남긴다.

> 코드 예시와 수치는 의도를 보여 주는 스케치다. 실측이 우선한다.


## 결정 기록

### WI-4.002 디스플레이 잠자기 (2026-11-24)

| 버전 | 시험 | 결과 |
| ---- | ---- | ---- |
| 0.0.30 | `pmset displaysleepnow` (Claude가 실행) | 전송이 계속됨. 프레임버퍼 `IOPowerManagement`가 `CapabilityFlags=0, CurrentPowerState=0` — 전원 관리 트리에 없음 |
| 0.0.31 | KDK 심볼: IOFramebuffer 기본 클래스는 PM 등록을 하지 않고 IONDRVFramebuffer가 `initForPM()`에서 `PMinit`·`joinPMtree`·`registerPowerDriver`·`changePowerStateTo`를 직접 함 → 같은 3단계 등록 | 등록 성공: `CurrentPowerState=2, CapabilityFlags=49152` |
| 0.0.31 | `pmset displaysleepnow` (Claude가 실행) | 화면이 꺼졌다가 약 1초 뒤 켜짐(powerd `assertionActivityValid:1`). Claude 앱 활동이 깨운 것으로 보임 → 판정 불가 |
| 0.0.31 | 사용자 수동 시험 | 화면이 멈추고 마우스·원격 연결 끊김, 키로 복구 안 됨 → 재부팅. **원인은 시스템 잠자기**: pmset 로그 `21:26:00 Entering Sleep state due to 'Idle Sleep'` (`pmset sleep 1`, R-09). QEMU VM은 S3에서 키 입력으로 깨어나지 않는다 |

| 0.0.31 | `sudo pmset -a sleep 0` 뒤 사용자 수동 `pmset displaysleepnow` | 시스템 잠자기 없음. pmset 로그 `21:39:00 Display is turned off` → `21:39:52 Display is turned on`, 키로 깨우기·로그인 정상, 패닉 없음. **그러나 `VVDisplayPower`가 생기지 않았고 flush가 계속됨** — `setAttribute(powr)=0`이 오지 않았다 |
| 0.0.32 | KDK 디스어셈블: `IODisplay::setPowerState`는 디스플레이 파라미터(`AudioMuteAndScreenBlank`, 밝기)를 바꾸고, 프레임버퍼 자체의 PM 상태는 디스플레이 잠자기에서 내려가지 않는다. → `setAttributeForConnection`을 재정의해 모든 호출을 `VVFbPowerTrace`(최근 24개)에 남기고 `kConnectionPower`(값 0 = 꺼짐)로 전송을 멈춘다 | 사용자 시험: pmset 로그 `22:24:27 Display is turned off` → `22:25:27 on`이지만 화면은 그대로(사용자: 잠자기에 들어가지 않음). `VVFbPowerTrace n=11` — 모두 부팅 때 호출이고 잠자기 동안 프레임버퍼로 온 호출이 없다 |
| 0.0.33 | KDK 디스어셈블: `IONDRVFramebuffer::initForPM`은 `registerPowerDriver` 뒤 `temporaryPowerClampOn()` + `changePowerStateTo(1)`을 한다. `IOFramebuffer::checkPowerWork`는 자기 PM 상태가 바뀔 때 `setAttribute('pwrs', getPowerState())`를 부른다. 우리는 `changePowerStateTo(2)`로 고정해 자식(AppleDisplay)이 내려가도 상태가 2로 남았다 → IONDRV와 같게 바꾸고 `pwrs < 2`면 전송을 멈춘다 | 사용자 시험: `pmset displaysleepnow` 즉시 화면이 멈추고(전송 중지) 30초 뒤 키 입력으로 잠금 화면 표시, 패닉 없음. `VVFbPowerTrace … set(pwrs)=1 set(powr)=1 set(pwrs)=2 set(powr)=2`, `VVDisplayPower on sleeps=1 wakes=1`. **전원 경로는 동작한다.** 다만 호스트 창이 검게 되지 않고 마지막 화면이 남는다 |
| 0.0.34 | 잠자기 동안 `SET_SCANOUT(resource 0)`으로 scanout을 끊고, 깨어나면 같은 resource를 다시 연결한 뒤 전체 전송. 둘 다 전송 루프 스레드에서만 한다 | 사용자 시험: 호스트 창이 그대로(마지막 화면). 오류 로그·flush-errors 없음. QEMU 10.0 소스(`virtio_gpu_set_scanout` → `virtio_gpu_disable_scanout` → `dpy_gfx_replace_surface(NULL)`)대로면 "Display output is not active." 자리표시 화면이 떠야 한다 → 명령이 실제로 갔는지 확인할 기록이 없다 |
| 0.0.35 | 끊기·다시 연결 결과를 로그로 남기고 `VVScanout`에 `blanks=` 횟수를 넣는다 | 사용자 시험: 변화 없음. `VVScanout … blanks=0`, 로그에 끊기 기록 없음, `VVDisplayPower on sleeps=1 wakes=1`. 원인: 잠자기 때 `set(pwrs)=1` 직후 `set(powr)=1`이 오고, `powr≠0`을 켜짐으로 처리해 바로 다시 켰다. 로그의 off→on 한 쌍은 잠자기 순간에 함께 찍힌 것이고 깨울 때가 아니었다. 0.0.33의 "화면 멈춤"도 전송 중지가 아니라 WindowServer가 그리기를 멈춘 것이다 |
| 0.0.36 | 켜기·끄기 판단을 `pwrs`(전원 단계)로만 한다. `powr` 값은 기억만 하고, `kConnectionPower` 처리는 뺀다(오지 않음) | 사용자 시험 1회: 잠자기 동안 호스트 창에 "Display output is not active." 표시, 키 입력으로 깨우면 잠금 화면, 패닉 없음. 로그 `scanout 0 disabled for display sleep -> 0x0` → `scanout 0 -> resource 2 after display wake -> 0x0`, `blanks=1`, `flush-errors=0`. **동작 확인.** AC-09 10회 반복은 남음 |

조치: 시스템 잠자기는 사용자가 껐다 (`sleep 0`, Q-04). 0.0.36에서 잠자기·깨우기가 동작한다.

**AC-09 결과 (2026-11-24 22:58–23:08, 0.0.36)**: `scripts/display-sleep-cycles.sh 10` — 10회 모두 ok (매회 `sleeps`·`wakes`·`blanks`가 1씩 늘고 `flush-errors=0`), 패닉 없음. 로그는 `docs/research/p4-display-sleep-ac09.log`, 로그인 뒤 스크린샷은 `docs/screenshots/p4/display-sleep-01..10.png`(모두 2560×1600).

제약:
- 스크린샷에는 바탕화면과 메뉴 막대만 있고 창은 없다. 터미널에 화면 기록 권한이 없을 때 `screencapture`가 창을 빼고 찍는 동작이다. 해상도·렌더링 확인 근거로만 쓴다.
- 녹화(`docs/movie/p4-display-sleep.mov`)는 만들지 못했다. 게스트 `screencapture -V`가 실패한다(WI-2.007과 같은 제약). 호스트 녹화가 필요하다.

### WI-4.003 시스템 잠자기 요청 (2026-11-24)

| 버전 | 시험 | 결과 |
| ---- | ---- | ---- |
| 0.0.31 | (의도하지 않음) `pmset sleep 1` 상태에서 Idle Sleep 진입 21:26:00 | VM이 키 입력으로 깨어나지 않아 사용자가 재시작. 패닉 보고서 없음. 깨어난 뒤 동작은 보지 못함 |
| 0.0.36 | 시험 전 상태: `VVDisplayPower on sleeps=11 wakes=11`, `VVScanout running resource=2 … flush-errors=0 blanks=11`, `VVInitResult driver-ok` | 사용자 시험: `pmset sleepnow`. GTK 메뉴에 QEMU 모니터가 없어 `system_wakeup`을 못 했다. pmset 로그: `23:15:39 Entering Sleep (Software Sleep)` → `23:17:32 DarkWake` → `23:17:35 FullWake, Display is turned on` → `23:18:16 power button > 4 sec`(사용자 재시작). **시스템은 스스로 깨어났고 패닉 보고서는 없다.** 깨어난 뒤 화면·드라이버 상태는 재시작으로 지워져 확인하지 못했다 |
| 0.0.37 | 진단: 프레임버퍼 전원 단계가 바뀔 때마다 PCI 명령 레지스터, virtio `device_status`, controlq `queue_enable`을 로그에 남긴다. 깨어난 뒤 QEMU가 장치를 reset했는지 판단한다 | 사용자 시험: `pmset sleepnow` → `23:25:03 DarkWake` → `23:25:33 Entering Sleep`. 이후 깨어남 기록 없음, 키 입력으로 깨어나지 않음, 원격 접속 불가 → `23:32:59` 강제 재시작. 패닉 보고서 없음. 깨어나지 않았으므로 진단 로그는 얻지 못했다 |

정리:
- 잠들기(시스템 잠자기 요청 응답)는 2회 모두 패닉 없이 진행됐다.
- 깨어나기는 이 환경에서 시험할 수단이 없다. 키 입력은 S3의 QEMU VM을 깨우지 못하고(2회 중 1회는 23:17:32에 스스로 깨어났으나 원인 불명), GTK 메뉴에 QEMU 모니터가 없어 `system_wakeup`을 보낼 수 없다. 사용자는 시스템 잠자기를 쓰지 않는다(`sleep 0`, Q-04).

결정(2026-11-24, 사용자): 잠들기 패닉 없음으로 WI-4.003을 닫고 깨어나기는 미시험으로 남긴다. 깨어나기를 시험하려면 호스트 QEMU 실행 옵션에 모니터(`-monitor stdio` 등)를 추가해 `system_wakeup`을 보낸다. 0.0.37의 전원 단계 진단 로그는 그대로 둔다.

### WI-4.001 하드웨어 커서 (2026-11-24)

| 버전 | 내용 | 결과 |
| ---- | ---- | ---- |
| 0.0.38 | `getAttribute(crsr)=1`. `setCursorImage`: 64×64 `kIO32ARGBPixelFormat`(메모리 B,G,R,A → `B8G8R8A8_UNORM`)으로 `convertCursorImage` → 커서 resource에 TRANSFER → 보이는 중이면 cursorq `UPDATE_CURSOR`. `setCursorState`: 왼쪽 위+핫스팟 = 포인터 위치로 `MOVE_CURSOR`, 숨기면 resource 0 `UPDATE_CURSOR`. cursorq는 응답을 기다리지 않고 게이트 안에서 링에 올린 뒤 다음 제출·인터럽트 때 회수한다. 요청 칸은 디스크립터 번호(`SplitRing::nextFreeHead`)로 정해 링에 올리기 전에 채운다. boot-arg `-vvideoswcursor`면 미지원 보고(소프트웨어 커서, REQ-051). 단위 테스트 72개(커서 3, 링 1 추가) | 사용자 시험: 화살표 정상·부드럽게 움직임, 창 가장자리·링크 손 모양 정상, 클릭 위치 정상. **I 커서가 두 개로 보임**, **커서가 소프트웨어 커서보다 큼** |
| 0.0.39 | I 커서: `convertCursorImage`는 행을 커서 폭으로 붙여 쓴다(폭 64인 화살표만 정상이었던 이유). 변환 뒤 행을 64픽셀 간격으로 다시 놓는다(`vv::cursor::repackRows`, 단위 테스트 2개) | 사용자 시험: I 커서 정상. 커서는 50% 정도가 맞다. 게스트 2560×1600(HiDPI, UI 1280×800), 호스트 1920×1200에서 QEMU 창 1280×800 |
| 0.0.40 | 커서 크기 비율: 50이면 2×2 평균으로 절반으로 줄이고 장치 핫스팟도 절반. 포인터 위치는 원래 핫스팟으로 구한다. 설정 순서: boot-arg `vvideo_cursor_scale` > Info.plist personality `VVCursorScale`(기본 50, EFI 수정 없이 kext 설치로 정함) > 100. 단위 테스트 2개 | 사용자 시험: 크기·클릭 위치 정상. `VVCursor resource=2 images=9 moves=330 hides=10 drops=0 rejects=0 scale=50`, cursorq 완료 346, 장치 오류 0, 패닉 없음 |

커서 크기: QEMU 10.0 `ui/gtk.c gd_cursor_define`은 커서 이미지를 픽셀 크기 그대로 호스트 GDK 커서로 만든다(창 배율을 적용하지 않음). 게스트는 HiDPI 2560×1600이라 WindowServer가 2배 커서를 준다. 호스트 창이 게스트 화면을 축소해 보여 주면 화면만 줄고 커서는 그대로여서 커 보인다. 드라이버는 호스트 배율을 알 수 없다.
| 0.0.40 | 대체 경로: boot-args `keepsyms=1 -vvideoswcursor` (config.plist 백업 `config.plist.bak-20261125-wi4001`) | `get(crsr)=0`, `VVCursor resource=0 images=0 moves=0`, `cursorq=0`. 게스트 커서가 프레임버퍼에 그려져 화면과 함께 줄고 모양이 바뀜, 호스트 포인터도 보임(0.0.38 이전과 같은 동작). 패닉 없음. 시험 뒤 boot-args를 `keepsyms=1`로 되돌림 |

### WI-4.004 변경 감지 전송 (2026-11-25)

기준(0.0.40, 2560×1600 60 Hz 전체 전송, 정지 화면): `kernel_task` 약 6%. 전체 복사는 호스트(QEMU)가 한다.

| 버전 | 내용 | 결과 |
| ---- | ---- | ---- |
| 0.0.41 | `vv::dirty::scan`: 타일(기본 64)마다 shadow와 행 단위 비교, 바뀐 행은 shadow로 복사. band별 가장 왼쪽~오른쪽 바뀐 타일을 사각형 하나로, 가로 범위가 같은 이웃 band는 합침. 16개를 넘으면 전체 한 장. 바뀐 사각형만 `TRANSFER_TO_HOST_2D`(offset = y·pitch + x·4), 덮는 사각형 하나 `RESOURCE_FLUSH`, 바뀐 것이 없으면 명령 없음. 새 resource·깨어남·설정 변경 때 기준을 다시 잡는다(전체 전송). 설정: Info.plist `VVDirtyFlush`(기본 0)·`VVDirtyTile`(64) > boot-arg `vvideo_dirty`·`vvideo_dirty_tile`, 실행 중 전환은 `scripts/vvctl.sh dirty 0|1 [tile]`(관리자만, `setProperties`). 통계 `VVDirtyStats`. 측정 `scripts/measure-flush.sh <장면>` → `docs/research/p4-performance.log`. 단위 테스트 82개(변경 감지 6 추가) | 아래 측정 1·2 |

측정 1 — 정지 화면, 2560×1600, 타일 64, 각 30초 (`docs/research/p4-performance.log`):

| 변경 감지 | 게스트 `kernel_task` | 보낸 양 | 주기 | 비교 시간 |
| --------- | -------------------- | ------- | ---- | --------- |
| 끔 | 6.1% | 25,188 MiB (약 840 MiB/s, 전체 전송 약 54회/s) | 전체 | — |
| 켬 | 24.9% | 0 MiB (부분 1회, 건너뜀 1,425회) | 약 47회/s | 평균 5.2 ms/주기 |

해석: 정지 화면에서 호스트로 가는 전송·FLUSH는 0이 되지만, 게스트는 16 MiB 화면과 shadow를 매 주기 비교하느라 vCPU 한 개의 약 19%를 더 쓴다(메모리 대역 한계, 주기당 32 MiB 읽기).

측정 2 — 같은 조건 반복(사용자 실행): 게스트 6.2% / 25.0%로 같음. 호스트 QEMU CPU는 두 구간 모두 4.2~4.9%로 차이 없음.

결정: 기본값 꺼짐(`VVDirtyFlush=0`). 변경 감지에 가장 유리한 정지 화면에서도 호스트 이득이 보이지 않고 게스트 비용만 늘기 때문이다. 타이핑·동영상은 이득이 더 작으므로 재지 않았다. 정리는 `docs/research/p4-performance.md`.

### WI-4.005 virtqueue 선택 feature (2026-11-25)

장치 제안: `device features lo=0x30000013` — `INDIRECT_DESC`(28)·`EVENT_IDX`(29) 모두 제공. 드라이버는 둘 다 받지 않는다(`VirtioFeatures.hpp`).

실측(0.0.41, 2560×1600, 평소 사용 30초): controlq 명령 101.5건/s(화면 갱신 약 51회 × TRANSFER·FLUSH), cursorq 0.7건/s, 인터럽트 102건/s, 시간 초과 0.

결정(사용자 승인): **구현하지 않고 둘 다 끈다.** 켬·끔 비교 측정은 하지 않았다.
- `EVENT_IDX`: 완료 알림을 묶어 인터럽트를 줄이는 기능인데, 드라이버는 명령 하나를 보내고 완료를 기다린 뒤 다음을 보내므로 묶을 완료가 없다. 초당 약 100건의 인터럽트는 CPU 부담으로도 의미가 없다.
- `INDIRECT_DESC`: 명령 하나가 디스크립터 하나만 쓰게 하는 기능인데, controlq 64칸 중 최대 2칸만 쓰므로 부족할 일이 없다.
- 둘 다 링 처리 커널 코드를 바꿔야 하므로, 이득이 없는 변경으로 위험만 늘린다.
- 다시 볼 조건: 명령을 여러 개 동시에 처리하도록(비동기 제출) 바꿀 때.
