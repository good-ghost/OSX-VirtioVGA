# VirtioVGA 0.9.0

## English

First packaged release of VirtioVGA, a virtio-gpu 2D framebuffer driver (kext) for macOS 15 guests on QEMU/KVM (OSX-KVM).

### Install

Download `VirtioVGA-0.9.0.pkg` and `VirtioVGA-0.9.0.pkg.sha256`, then follow [docs/NEW-VM-INSTALL.en.md](https://github.com/good-ghost/OSX-VirtioVGA/blob/main/docs/NEW-VM-INSTALL.en.md):

1. Take a host disk snapshot of the VM.
2. `shasum -a 256 -c VirtioVGA-0.9.0.pkg.sha256`
3. `sudo installer -pkg VirtioVGA-0.9.0.pkg -target /`
4. `sudo "/Library/Application Support/VirtioVGA/opencore-setup.sh"` (backs up config.plist, adds the helper kext, sets csr-active-config 0x201)
5. Restart, then check with `"/Library/Application Support/VirtioVGA/vvstat"`.

The package is not signed.

### Features

- 10 standard modes (1024×768 – 2560×1600), the device's preferred mode, HiDPI, mode restore after reboot
- Automatic resolution change when the host QEMU window is resized (`vvresize` agent)
- 128 MB VRAM report, GPU name "Virtio GPU", EDID ("QEMU Monitor")
- Hardware cursor, display sleep, automatic device recovery, fallback to the boot framebuffer (IONDRV) on init failure

### Requirements

macOS 15.x on x86_64, QEMU `-device virtio-vga` (or `virtio-vga-gl`), OpenCore (tested 1.0.6, `SecureBootModel` Disabled recommended).

### Known limitations

- No Metal / GPU acceleration; transparency effects and Metal-drawn wallpapers do not work.
- Single display only (multi-output is on hold).
- Window minimize animation is slow in 2560×1600 HiDPI (software rendering).
- Replacing the kext requires a reboot; do not unload it while it drives the screen.

### Changes since the development builds

- Renamed from Virtio-Video (`VirtioVideo.kext`, `local.virtiovideo.*`) to VirtioVGA (`VirtioVGA.kext`, `local.virtiovga.*`). The installer removes the old kext and agent; `opencore-setup.sh` replaces the old OpenCore entries.

## 한국어

VirtioVGA의 첫 패키지 릴리스입니다. QEMU/KVM(OSX-KVM)의 macOS 15 게스트용 virtio-gpu 2D 프레임버퍼 드라이버(kext)입니다.

### 설치

`VirtioVGA-0.9.0.pkg`와 `VirtioVGA-0.9.0.pkg.sha256`을 내려받은 뒤 [docs/NEW-VM-INSTALL.md](https://github.com/good-ghost/OSX-VirtioVGA/blob/main/docs/NEW-VM-INSTALL.md)를 따라 주세요.

1. 호스트에서 VM 디스크 스냅샷을 만듭니다.
2. `shasum -a 256 -c VirtioVGA-0.9.0.pkg.sha256`
3. `sudo installer -pkg VirtioVGA-0.9.0.pkg -target /`
4. `sudo "/Library/Application Support/VirtioVGA/opencore-setup.sh"` (config.plist 백업, 보조 kext 추가, csr-active-config 0x201 설정)
5. 재부팅한 뒤 `"/Library/Application Support/VirtioVGA/vvstat"`로 확인합니다.

패키지는 서명되지 않았습니다.

### 기능

- 표준 해상도 10종(1024×768 ~ 2560×1600), 장치 선호 해상도, HiDPI, 재부팅 후 모드 복원
- 호스트 QEMU 창 크기에 맞춘 해상도 자동 전환(`vvresize` 에이전트)
- VRAM 128 MB 보고, GPU 이름 "Virtio GPU", EDID("QEMU Monitor")
- 하드웨어 커서, 디스플레이 잠자기, 장치 자동 복구, 초기화 실패 시 기존 화면(IONDRV)으로 복귀

### 요구 환경

macOS 15.x x86_64, QEMU `-device virtio-vga`(또는 `virtio-vga-gl`), OpenCore(1.0.6에서 확인, `SecureBootModel`은 Disabled 권장)

### 알려진 제약

- Metal(GPU 가속)이 없어 투명 효과와 Metal로 그리는 배경은 동작하지 않습니다.
- 모니터는 하나만 지원합니다(다중 출력은 보류).
- 2560×1600 HiDPI에서는 창 최소화 애니메이션이 느립니다(소프트웨어 렌더링).
- kext 교체는 재부팅으로 합니다. 화면을 맡은 동안 언로드하지 마세요.

### 개발 빌드와 달라진 점

- 이름을 Virtio-Video(`VirtioVideo.kext`, `local.virtiovideo.*`)에서 VirtioVGA(`VirtioVGA.kext`, `local.virtiovga.*`)로 바꿨습니다. 설치 프로그램이 예전 kext와 에이전트를 지우고, `opencore-setup.sh`가 예전 OpenCore 항목을 바꿉니다.
