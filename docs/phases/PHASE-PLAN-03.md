# PHASE-PLAN-03 — 모드와 VRAM

| 항목      | 내용                                                                   |
| --------- | ---------------------------------------------------------------------- |
| Phase     | P3                                                                     |
| 기간      | 11.5 Man-day                                                           |
| 관련 요구 | REQ-025, 026, 034, 036, 040~042, 044, 045, 060~063, NFR-006            |
| 선행      | P2 완료 (M1)                                                           |
| 완료 조건 | **M2** — 시스템 설정에서 해상도를 골라 바꿀 수 있고, VRAM이 128 MB로 표시된다 (AC-02, AC-03) |

## 1. 목표

사용자가 처음 요청한 두 문제(해상도 고정, VRAM 3 MB)를 해결한다. EDID로 디스플레이 식별 정보를 제공해, 배경 저장소의 디스플레이 키가 `FFFFFFFF-…`에서 바뀌는지도 확인한다 (REQUIREMENT §2.5).

## 2. 의존 관계와 실행 순서

```
WI-3.001 ─> WI-3.002 ─> WI-3.004 ─> WI-3.006 ─> WI-3.007
WI-3.003 ──────────────┘
WI-3.008, WI-3.009 (WI-3.002 이후 언제든)
WI-3.005 (WI-3.004 이후)
```

## 3. Work Item 상세

### WI-3.001 모드 테이블 계산 (1)

- 입력: 표준 모드 목록(REQ-040), 최대 2560×1600, 풀 128 MiB, 선호 크기·EDID 모드(WI-3.004에서 병합).
- 출력: `IODisplayModeID` 배열과 모드별 크기. 모드 ID는 표 순서로 고정해 재부팅 간 안정적으로 유지한다 (WI-3.007 복원에 필요).
- 순수 함수로 `kext/include/`에 두고 단위 테스트: 상한 초과 제외, 풀 초과 제외, 중복 제거, 정렬.

### WI-3.002 모드 전환 (2)

순서 (REQ-034):

1. 새 크기 버퍼 할당(T-01 방식) → `RESOURCE_CREATE_2D` → `ATTACH_BACKING`
2. flush 타이머 일시 정지
3. `SET_SCANOUT`(새 resource) → 전체 transfer·flush
4. 이전 resource `DETACH_BACKING` → `UNREF`, 이전 버퍼 해제
5. flush 재개, IOFramebuffer에 변경 통지

- 중간 실패 시 이전 resource로 되돌린다.
- 수락 기준: REQ-040 모드 각각으로 전환 → `screencapture` 크기가 모드와 같음 (AC-03), 패닉·정지 없음.

### WI-3.003 EDID 협상·검증·제공 (2)

- `VIRTIO_GPU_F_EDID` 협상(호스트 `edid=on` 기본값 확인됨) → `GET_EDID(scanout 0)`.
- 검증: 크기 ≤ 1024, 128바이트 블록 단위, 헤더 `00 FF FF FF FF FF FF 00`, 블록별 체크섬 (단위 테스트).
- 제공: `hasDDCConnect` true, `getDDCBlock`으로 블록 전달.
- 확인: 적용 후 `~/Library/Application Support/com.apple.wallpaper/Store/Index.plist`의 디스플레이 키, `system_profiler`의 디스플레이 이름.

### WI-3.004 선호 해상도·EDID 모드 병합 (1)

- display info 선호 크기(기본 1280×800, 호스트 `xres/yres`) + EDID detailed timing → WI-3.001 입력에 병합.
- 선호 크기를 `kDisplayModeDefaultFlag`로 표시.

### WI-3.005 디스플레이 변경 이벤트 처리 (1)

- config 변경 인터럽트 → `events_read` 확인 → `VIRTIO_GPU_EVENT_DISPLAY`면 `GET_DISPLAY_INFO` 재조회 → `events_clear`에 써서 지움.
- GTK 창 크기 변경으로 이벤트가 오는지 실측하고 결과를 기록 (HST-004). 자동 전환은 P6(WI-6.001).
- 모드 목록 갱신 시 IOFramebuffer에 연결 변경 통지.

### WI-3.006 HiDPI 모드 (1.5)

- 2560×1600 → UI 1280×800, 1920×1200 → 960×600 등 2배 배율 모드를 IOGraphics가 인식하는 방식으로 보고한다. 플래그·속성은 IOGraphics 오픈소스와 실측으로 확정한다.
- 수락 기준: 시스템 설정 > 디스플레이에 배율 선택지가 보이고 선택된다 (스크린샷).

### WI-3.007 선택 모드 복원 (1)

- macOS가 저장하는 디스플레이 설정은 디스플레이 식별자와 모드 ID에 묶인다. WI-3.003의 EDID 식별자와 WI-3.001의 고정 모드 ID로 복원되는지 확인한다.
- 수락 기준: 1920×1080 선택 → 재부팅 → 1920×1080으로 시작.

### WI-3.008 VRAM 보고 (1.5)

- `IOFBMemorySize` = 128 MiB (풀 상한).
- `system_profiler SPDisplaysDataType`의 "VRAM (Total)"이 어떤 IORegistry 속성을 읽는지 조사한다. 후보는 provider(PCI 장치)의 `VRAM,totalMB`, `VRAM,totalsize`류 속성이며, 확인하지 못한 추정이다.
- 수락 기준: `system_profiler`에 VRAM 128 MB, "Kernel Extension Info"에 이 드라이버 (AC-02). 비공개 속성 의존으로 표시가 안 되면 R-08 대응대로 사용자 승인을 받아 기준을 조정한다.

### WI-3.009 드라이버 상태 IORegistry 공개 (0.5)

- 속성: 협상 feature, scanout 수, 현재 모드, 큐 제출/완료 수, 오류 수, flush 수.
- 값은 주기적으로가 아니라 변경 시 또는 일정 간격(예: 1초)으로 갱신해 부하를 줄인다.

## 4. 검증 흐름과 기록

- 단위 테스트: 모드 계산, EDID 검증.
- 실기 확인: 모드별 스크린샷(`docs/screenshots/p3/mode-<w>x<h>.png`), 시스템 설정 조작 녹화(`docs/movie/p3-modes.mov`, 자막, 화면 전환 간격 1.5초).
- 적용 전/후 `collect-env.sh` 결과 비교.

## 5. 리스크

| 리스크                                         | 대응                                         |
| ---------------------------------------------- | -------------------------------------------- |
| VRAM 표시가 비공개 속성에 의존 (R-08)          | 조사 결과 기록, 기준 조정은 사용자 승인      |
| 큰 모드에서 물리 연속 할당 실패 (T-01)          | 부팅 초기에 풀을 예약하는 방식 검토          |
| HiDPI 보고 방식이 문서화되어 있지 않음          | IOGraphics 오픈소스 참고, 실측               |

> 코드·속성 이름은 의도를 보여 주는 스케치다. 실제 구현은 SDK 헤더, IOGraphics 동작, 실측이 우선한다.
