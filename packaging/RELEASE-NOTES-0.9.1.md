# VirtioVGA 0.9.1

## English

### Changes

- The hardware cursor is shown at full size again by default (`VVCursorScale` 100; 0.9.0 used 50). If the guest is shown at half size in the host window (for example a 2560×1600 HiDPI guest in a 1280×800 window), set the boot-arg `vvideo_cursor_scale=50`.

### Install / upgrade

1. `shasum -a 256 -c VirtioVGA-0.9.1.pkg.sha256`
2. `sudo installer -pkg VirtioVGA-0.9.1.pkg -target /`
3. Restart.

When upgrading from 0.9.0 you do not need to run `opencore-setup.sh` again (the helper kext did not change). For a new VM, follow [docs/NEW-VM-INSTALL.en.md](https://github.com/good-ghost/OSX-VirtioVGA/blob/main/docs/NEW-VM-INSTALL.en.md). The package is not signed.

## 한국어

### 바뀐 점

- 하드웨어 커서를 다시 원래 크기로 표시합니다(`VVCursorScale` 100, 0.9.0은 50). 호스트 창에서 게스트 화면을 절반으로 줄여 보는 경우(예: 2560×1600 HiDPI 게스트를 1280×800 창으로 볼 때)에는 boot-arg `vvideo_cursor_scale=50`을 넣어 주세요.

### 설치·업그레이드

1. `shasum -a 256 -c VirtioVGA-0.9.1.pkg.sha256`
2. `sudo installer -pkg VirtioVGA-0.9.1.pkg -target /`
3. 재부팅합니다.

0.9.0에서 올릴 때는 `opencore-setup.sh`를 다시 실행하지 않아도 됩니다(보조 kext는 바뀌지 않았습니다). 새 VM은 [docs/NEW-VM-INSTALL.md](https://github.com/good-ghost/OSX-VirtioVGA/blob/main/docs/NEW-VM-INSTALL.md)를 따라 주세요. 패키지는 서명되지 않았습니다.
