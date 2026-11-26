# 인수 기준 최종 점검 (WI-5.010, M3)

점검일: 2026-11-25. 대상: kext 0.0.42 (`local.virtiovideo.VirtioVideo`), 보조 kext `VirtioVideoNdrvBlocker`. 환경: macOS 15.8.3 게스트, QEMU 10.0.13 `-device virtio-vga`(GTK), vCPU 4, boot-args `keepsyms=1`.
기준은 [REQUIREMENT.md §9.1](REQUIREMENT.md). 이번 점검의 원본 기록은 [research/p5-acceptance.log](research/p5-acceptance.log).

## 결과

| ID | 기준 (요약) | 결과 | 확인 방법·근거 (0.0.42 재확인 여부) |
| -- | ----------- | ---- | ----------------------------------- |
| AC-01 | 이 드라이버가 디스플레이에 붙고 `IONDRVFramebuffer`는 없다 | 통과 | 재확인. `ioreg -c IOFramebuffer`에 `VirtioVideoFramebuffer` 하나, 그 아래 `display0`·`AppleDisplay`. `IONDRVFramebuffer` 0개 |
| AC-02 | VRAM이 REQ-060 값으로 표시된다 | 통과 | 재확인. `system_profiler`: VRAM (Total) 128 MB, Vendor 0x1af4, Device 0x1050 |
| AC-03 | REQ-040 모드가 보이고, 바꾸면 실제 해상도가 바뀐다 | 통과 | 재확인. `scripts/ac03-modes.swift`: 모드 10개를 차례로 바꾸고 화면 캡처 크기 = 모드 픽셀 크기 10/10, 드라이버 scanout도 같은 크기로 바뀜(`setDisplayMode` 로그). 원래 모드로 복원 |
| AC-04 | 빨강·초록·파랑·흰색이 뒤바뀌지 않는다 | 통과 | 재확인. `scripts/color-bands.swift`로 게스트 전체 화면에 색 띠를 띄우고, 사용자가 호스트 QEMU 창에서 순서 정상 확인. 표시 중 flush·장치 오류 0 |
| AC-05 | boot-arg로 끄면 기존 1280×800 부트 프레임버퍼로 부팅된다 | 통과 | 재확인. `-vvideooff` 부팅: `IONDRVFramebuffer` 1개, 우리 프레임버퍼 0개, `VVNdrvBlocked = skipped:-vvideooff`, 1280×800, VRAM 3 MB, 패닉 0 |
| AC-06 | 초기화를 강제로 실패시켜도 부팅이 계속되고 IONDRV가 남는다 | 통과 | 재확인(장치 초기화 단계). `vvideo_fail=2` 부팅: 로그 `init failed: injected:bring-up` → `handing the screen back to IONDRV`, `IONDRVFramebuffer` 1개, 1280×800, 패닉 0. 전송 계층(1)·프레임버퍼(3) 단계는 0.0.25 결과 (WI-2.008) |
| AC-07 | 배경·투명 효과·Metal 전후 비교 기록 | 통과(기록) | 0.0.42에서 기록 (WI-5.007): ① 사진 정상 ② Sequoia 여전히 안 그려짐(흰색 대체) ③ 단색 16 px 테두리 사라짐 ④ 메뉴 막대·Dock 불투명 ⑤ Metal 0개. ②④⑤는 범위 밖(Q-12) — [PHASE-PLAN-05](phases/PHASE-PLAN-05.md) 결정 기록 |
| AC-08 | NFR-001(연속 사용)·NFR-002(모드 전환 100회) 통과 | 통과(한계 있음) | NFR-002 재확인: 100/100 ok, 패닉 0, 오류 0, 드라이버 객체 수 전후 같음. NFR-001: 0.0.42 12시간(계획 24시간에서 사용자 결정으로 단축) 패닉 0·오류 0·객체 증가 없음. 화면을 켜고 쓴 시간은 약 43분이고 나머지는 디스플레이 잠자기였다 — 사용자가 한계와 함께 인정 ([research/p5-soak.log](research/p5-soak.log)) |
| AC-09 | 디스플레이 잠자기 → 깨우기 → 잠금 → 로그인 10회, 깨짐·검은 화면·패닉 없음, 해상도 유지 | 통과 | 재확인. `scripts/display-sleep-cycles.sh 10`: 10/10 ok, 매 회차 로그인 뒤 캡처 2560×1600, flush 오류 0, 패닉 0 ([research/p4-display-sleep-ac09.log](research/p4-display-sleep-ac09.log)) |

## 판정

AC-01~AC-09 모두 통과로 본다. AC-08은 NFR-001 시험 조건(24시간, 일반 사용)을 다 채우지 못한 것을 사용자가 인정한 결과다.

## 남은 사항

- **시연 녹화 없음**: 게스트 `screencapture -V`가 실패해 호스트 녹화가 필요하다. 사용자 결정으로 녹화 없이 마감했다(`docs/movie/m3-demo.mov` 없음). WI-2.007의 녹화 항목도 같은 이유로 남아 있다.
- **AC-03 화면 확인**: 시스템 설정 > 디스플레이 화면을 이번에 캡처하지 못했다(게스트에 화면 기록 권한이 없어 캡처에 다른 앱 창이 나오지 않음). 모드 목록은 시스템 설정이 쓰는 CoreGraphics 목록으로 확인했고, 설정 화면에서의 전환은 0.0.26에서 사용자가 확인했다.
- **모드 목록 표시 방식**: macOS는 픽셀 크기마다 모드를 하나씩만 보여 준다. 1440×900·1600×900·1680×1050·1920×1080·1920×1200·2560×1440·2560×1600은 HiDPI(절반 크기 UI)로 보인다. REQ-040의 픽셀 크기 10개는 모두 있다. WI-5.002 때는 같은 API로 17개(1:1과 HiDPI 모두)가 보였다. 차이의 원인은 확인하지 못했다.
- **REQ-062(S)**: `system_profiler`의 "Kernel Extension Info"에 드라이버가 나오지 않는다. 미충족으로 남는다.
- **알려진 제약**: 창 최소화 애니메이션이 HiDPI에서 느리다(Metal 없음, [research/p5-minimize.md](research/p5-minimize.md)). 나머지는 [INSTALL.md §8](INSTALL.md).
- **고정 메모리**: 12시간 시험에서 +약 299 MiB, 모드 전환 100회에서 +약 39 MiB(10,048 페이지). 드라이버 객체 수는 변하지 않아 드라이버 누수로 보지 않지만 원인은 확인하지 못했다.
