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
