# VirtioVGA

[English](README.en.md) | 한국어

OSX-KVM(QEMU/KVM)에서 실행되는 macOS 게스트용 **virtio-gpu 2D 프레임버퍼 드라이버(kext)**입니다.

드라이버가 없으면 게스트는 펌웨어(OVMF GOP)가 넘겨준 부트 프레임버퍼(`IONDRVFramebuffer`)로 화면을 그립니다. 이때 해상도가 하나로 고정되고 VRAM은 "3 MB"로 표시됩니다. VirtioVGA는 virtio-gpu 명령으로 화면을 구성하는 `IOFramebuffer`를 제공합니다.

## 기능

- 해상도 10종(1024×768 ~ 2560×1600), 장치 선호 해상도, HiDPI, 재부팅 후 모드 복원
- 호스트 QEMU 창 크기에 맞춘 해상도 자동 전환(에이전트 `vvresize`)
- VRAM 128 MB 보고, GPU 이름 "Virtio GPU", EDID로 모니터 식별("QEMU Monitor")
- 하드웨어 커서(virtio cursorq), boot-arg로 소프트웨어 커서 전환
- 디스플레이 잠자기: 전송을 멈추고 화면을 끈 뒤, 깨우면 복원
- 장치 오류 때 자동 복구, 초기화에 실패하면 기존 화면(IONDRV)으로 복귀
- 읽기 전용 상태 조회 도구 `vvstat`

Metal(GPU 가속)은 범위 밖입니다. 투명 효과와 Metal로 그리는 배경(Sequoia 등)은 동작하지 않습니다. 다중 모니터는 지원하지 않습니다(WI-6.002 보류).

## 대상 환경

- macOS 15.x x86_64 (확인: 15.8.3 `24H241`), OpenCore 1.0.6. Lilu 1.6.8·WhateverGreen 1.6.7과 함께 쓸 수 있음을 확인했습니다.
- QEMU 10.0.13 `-device virtio-vga`(또는 `virtio-vga-gl`), GTK 디스플레이
- SIP `csr-active-config` 0x201(Kext Signing만 해제)

## 설치

- **새 VM**: GitHub Releases의 `VirtioVGA-0.9.0.pkg`로 설치합니다. 순서는 [docs/NEW-VM-INSTALL.md](docs/NEW-VM-INSTALL.md)를 따라 주세요. 패키지를 설치하고 `opencore-setup.sh`를 한 번 실행한 뒤 재부팅하면 됩니다.
- **소스에서 빌드해 설치·교체**(개발용), 설정값, 롤백: [docs/INSTALL.md](docs/INSTALL.md)
- **문제가 생겼을 때**: [docs/DEBUGGING.md](docs/DEBUGGING.md)

## 빌드

```bash
scripts/build.sh
```

```bash
scripts/test.sh
```

```bash
packaging/build-pkg.sh
```

`build/VirtioVGA.kext`, `build/VirtioVGANdrvBlocker.kext`, `build/pkg/VirtioVGA-<버전>.pkg`가 만들어집니다. Xcode(명령줄 도구, macOS SDK)가 필요합니다. 패키지는 서명하지 않습니다.

## 문서

| 문서 | 내용 |
| ---- | ---- |
| [docs/NEW-VM-INSTALL.md](docs/NEW-VM-INSTALL.md) | 새 VM에 pkg로 설치하기 |
| [docs/INSTALL.md](docs/INSTALL.md) | 개발용 설치·교체, 설정, 롤백, 알려진 제약 |
| [docs/DEBUGGING.md](docs/DEBUGGING.md) | 상태 조회, 로그, 패닉 분석 |
| [docs/REQUIREMENT.md](docs/REQUIREMENT.md) | 요구사항 (v1.4) |
| [docs/PROJECT-PLAN.md](docs/PROJECT-PLAN.md) | 프로젝트 계획과 진행 상태 |
| [docs/ACCEPTANCE.md](docs/ACCEPTANCE.md) | 인수 기준 최종 점검 결과 |

`docs/` 바로 아래 문서에는 영어판(`*.en.md`)이 같은 폴더에 있습니다.

## 디렉터리

| 경로                | 용도                                                         |
| ------------------- | ------------------------------------------------------------ |
| `kext/`             | 본 드라이버 소스. `kext/include/vv/`는 커널에 의존하지 않는 로직(단위 테스트 대상) |
| `kext-helper/`      | IONDRV 차단 보조 kext (OpenCore 주입)                        |
| `tools/`            | 사용자 공간 도구 (`vvstat.c`, `vvresize.swift`)               |
| `packaging/`        | 설치 패키지 빌드, OpenCore 설정·제거 스크립트                 |
| `scripts/`          | 빌드, 시험, 측정, 환경 수집, 패닉 요약                       |
| `tests/`            | 단위 테스트 (`scripts/test.sh`)                              |
| `docs/`             | 요구사항, 계획, 설치·디버깅 문서                             |

## 상태 (2026-11-25, 0.9.0)

- P0~P5 완료, 인수 기준 AC-01~09 통과(M3). P6의 창 크기 자동 전환(WI-6.001) 완료, 다중 모니터(WI-6.002) 보류
- 0.9.0에서 이름을 Virtio-Video(`VirtioVideo.kext`)에서 VirtioVGA(`VirtioVGA.kext`)로 바꾸고 설치 패키지를 추가했습니다.

## 라이선스

MIT — [LICENSE](LICENSE)
