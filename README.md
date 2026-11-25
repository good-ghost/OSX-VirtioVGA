# Virtio-Video

OSX-KVM(QEMU/KVM)에서 실행되는 macOS 게스트용 **virtio-gpu 2D 프레임버퍼 드라이버(kext)**다.

드라이버 없이는 게스트가 펌웨어(OVMF GOP)가 넘겨준 부트 프레임버퍼(`IONDRVFramebuffer`)로 화면을 그린다. 해상도가 하나로 고정되고 VRAM이 "3 MB"로 표시된다. 이 드라이버는 virtio-gpu 명령으로 화면을 구성하는 `IOFramebuffer`를 제공한다.

## 기능

- 해상도 10종(1024×768 ~ 2560×1600)과 장치 선호 해상도, HiDPI, 재부팅 후 모드 복원
- VRAM 128 MB 보고, EDID로 모니터 식별("QEMU Monitor")
- 하드웨어 커서(virtio cursorq), boot-arg로 소프트웨어 커서 전환
- 디스플레이 잠자기: 전송을 멈추고 화면을 끈 뒤 깨우면 복원
- 장치 오류 때 자동 복구, 초기화 실패 때 기존 화면(IONDRV)으로 복귀
- 읽기 전용 상태 조회 도구 `scripts/vvstat.sh`

Metal(GPU 가속)은 범위 밖이다. 투명 효과와 Metal로 그리는 배경(Sequoia 등)은 동작하지 않는다.

## 대상 환경

- macOS 15.x x86_64 (확인: 15.8.3 `24H241`), OpenCore 1.0.6, Lilu 1.6.8·WhateverGreen 1.6.7과 함께 동작 확인
- QEMU 10.0.13 `-device virtio-vga`, GTK 디스플레이
- SIP `csr-active-config` 0x201(Kext Signing만 해제)

## 빠른 시작

1. 빌드와 시험: `scripts/build.sh`, `scripts/test.sh`
2. 설치·설정·롤백: [docs/INSTALL.md](docs/INSTALL.md) — 본 드라이버는 `/Library/Extensions`(보조 kernel collection), 보조 kext는 OpenCore로 넣는다. 교체는 재부팅으로 한다.
3. 확인: `scripts/vvstat.sh`
4. 문제가 생기면: [docs/DEBUGGING.md](docs/DEBUGGING.md)

## 문서

- 요구사항: [docs/REQUIREMENT.md](docs/REQUIREMENT.md) (v1.3)
- 프로젝트 계획과 진행 상태: [docs/PROJECT-PLAN.md](docs/PROJECT-PLAN.md), Phase별 상세와 결정 기록: [docs/phases/](docs/phases/)
- 측정·조사 자료: [docs/research/](docs/research/)

## 디렉터리

| 경로                | 용도                                                         |
| ------------------- | ------------------------------------------------------------ |
| `kext/`             | 본 드라이버 소스. `kext/include/vv/`는 커널에 의존하지 않는 로직(단위 테스트 대상) |
| `kext-helper/`      | IONDRV 차단 보조 kext (OpenCore 주입)                        |
| `tools/`            | 사용자 공간 도구 (`vvstat.c`)                                |
| `scripts/`          | 빌드, 시험, 측정, 환경 수집, 패닉 요약                       |
| `tests/`            | 단위 테스트 (`scripts/test.sh`)                              |
| `docs/`             | 요구사항, 계획, 설치·디버깅 문서                             |
| `docs/research/`    | 조사·측정 기록                                               |
| `docs/screenshots/` | 검증 스크린샷                                                |
| `docs/movie/`       | 검증 녹화                                                    |

## 상태 (2026-11-25)

- kext 0.0.42. P0~P4 완료. P5(안정화와 문서) 진행 중: 24시간 시험(WI-5.001), 성능 측정(WI-5.004), 인수 기준 최종 점검(WI-5.010)이 남았다.
- 녹화가 필요한 항목은 게스트에서 녹화할 수 없어 호스트 녹화를 기다린다(WI-2.007).

## 라이선스

MIT — [LICENSE](LICENSE)
