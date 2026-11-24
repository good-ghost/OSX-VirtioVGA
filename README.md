# Virtio-Video

OSX-KVM(QEMU/KVM)에서 실행되는 macOS 게스트용 **virtio-gpu 2D 프레임버퍼 드라이버(kext)** 프로젝트다.

현재 게스트는 전용 드라이버 없이 펌웨어(OVMF GOP)가 넘겨준 부트 프레임버퍼(`IONDRVFramebuffer`)로 화면을 그린다. 그래서 해상도가 하나로 고정되고, VRAM이 프레임버퍼 크기(1280×800×4 = 4,096,000 바이트)인 "3 MB"로 표시된다. 이 프로젝트는 virtio-gpu 명령으로 화면을 구성하는 `IOFramebuffer` 드라이버를 만들어 이 문제를 해결하는 것을 목표로 한다.

- 요구사항: [docs/REQUIREMENT.md](docs/REQUIREMENT.md) (v1.0)
- 프로젝트 계획: [docs/PROJECT-PLAN.md](docs/PROJECT-PLAN.md) — Phase P0~P6, WI 51개
- Phase별 상세 계획: [docs/phases/](docs/phases/)
- 빌드: `scripts/build.sh`, 단위 테스트: `scripts/test.sh`
- 환경 수집·비교: `scripts/collect-env.sh --save <label>`, `scripts/compare-env.sh <이전> <이후>` (읽기 전용, sudo 불필요)
- 설치·비활성화·롤백: [docs/INSTALL.md](docs/INSTALL.md)

## 디렉터리

| 경로               | 용도                                                  |
| ------------------ | ----------------------------------------------------- |
| `kext/`            | 커널 확장 소스 (IOFramebuffer 하위 클래스, virtio 전송) |
| `tools/`           | 사용자 공간 진단 도구                                 |
| `scripts/`         | 환경 수집, 빌드, 설치·롤백 보조 스크립트              |
| `tests/`           | 단위 테스트(사용자 공간에서 돌 수 있는 로직), 검증 스크립트 |
| `docs/`            | 요구사항, 설계, 조사 자료                             |
| `docs/research/`   | 사양·조사 메모                                        |
| `docs/screenshots/`| 검증 스크린샷                                         |
| `docs/movie/`      | 검증 녹화                                             |

## 상태

요구사항 확정(REQUIREMENT.md v1.0), 계획 수립 완료(2026-11-23). 다음 작업은 P0(개발 환경)의 WI-0.001이다. 소스 코드는 아직 없다.
