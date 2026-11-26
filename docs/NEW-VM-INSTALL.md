# 새 VM에 설치하기 (pkg)

[English](NEW-VM-INSTALL.en.md) | 한국어

VirtioVGA 0.9.0부터는 설치 패키지(`VirtioVGA-<버전>.pkg`)로 설치합니다. 이 문서는 드라이버를 처음 넣는 VM을 기준으로 설명합니다. 소스에서 직접 빌드하거나 개별 파일을 손으로 바꾸는 방법은 [INSTALL.md](INSTALL.md)에 있습니다.

## 1. 준비물과 조건

| 항목 | 조건 |
| ---- | ---- |
| 게스트 | macOS 15.x, Intel(x86_64). 확인한 버전: 15.8.3 `24H241` |
| 호스트 | QEMU/KVM(OSX-KVM). 디스플레이 장치가 `-device virtio-vga`(또는 `virtio-vga-gl`)여야 합니다 |
| 부트로더 | OpenCore. 확인한 버전: 1.0.6. `Misc > Security > SecureBootModel`은 `Disabled`를 권합니다 |
| 권한 | 게스트의 관리자 계정(sudo) |
| 되돌릴 방법 | 호스트에서 만든 디스크 스냅샷 (아래 2단계) |

- 디스플레이 장치가 `virtio-vga` 계열이 아니면(예: `vmware-svga`, `VGA`) 드라이버가 붙지 않습니다. 호스트 QEMU 설정을 먼저 바꿔 주세요.
- 패키지는 서명되지 않았습니다. 설치 중 macOS가 "확인되지 않은 개발자" 경고를 보일 수 있습니다.
- 다중 모니터(`max_outputs` 2 이상)는 지원하지 않습니다. 장치가 출력을 여러 개 알려도 첫 번째 출력만 씁니다.

## 2. 호스트에서 디스크 스냅샷 만들기

드라이버는 커널에서 동작합니다. 문제가 생기면 부팅이 되지 않을 수 있으므로, **VM을 끈 상태에서** 스냅샷을 먼저 만들어 주세요. 켜진 상태의 qcow2에 `qemu-img snapshot`을 쓰면 이미지가 손상될 수 있습니다.

```bash
qemu-img snapshot -c before-virtiovga <게스트 디스크 이미지>.qcow2
```

OpenCore가 별도 이미지(예: `OpenCore.qcow2`)에 있다면 그 이미지도 함께 스냅샷을 만들어 주세요. 디스크가 qcow2가 아니면 이미지 파일을 복사해 둡니다.

## 3. 패키지 내려받기

GitHub 저장소 [good-ghost/OSX-VirtioVGA](https://github.com/good-ghost/OSX-VirtioVGA)의 Releases에서 `VirtioVGA-0.9.0.pkg`와 `VirtioVGA-0.9.0.pkg.sha256`을 내려받습니다. 내려받은 파일이 맞는지 확인합니다.

```bash
shasum -a 256 -c VirtioVGA-0.9.0.pkg.sha256
```

`OK`가 나와야 합니다. 두 파일이 같은 폴더에 있어야 합니다.

## 4. 패키지 설치

터미널에서 설치하는 방법을 권합니다(서명 경고를 거치지 않습니다).

```bash
sudo installer -pkg VirtioVGA-0.9.0.pkg -target /
```

Finder에서 설치하려면 파일을 오른쪽 클릭한 뒤 **열기**를 고릅니다. 패키지가 설치하는 것은 다음과 같습니다.

| 위치 | 내용 |
| ---- | ---- |
| `/Library/Extensions/VirtioVGA.kext` | 본 드라이버. 설치 뒤 `kmutil load -z -p`로 보조 kernel collection에 등록됩니다 |
| `/Library/Application Support/VirtioVGA/` | 보조 kext(`VirtioVGANdrvBlocker.kext`), `opencore-setup.sh`, `uninstall.sh`, `vvstat`, `vvresize` |
| `/Library/LaunchAgents/local.virtiovga.vvresize.plist` | 창 크기 자동 전환 에이전트 (로그인할 때 시작) |

"The install requires restarting now"라는 안내가 나와도 **아직 재부팅하지 마세요.** 다음 단계의 OpenCore 설정을 먼저 해야 합니다.

## 5. OpenCore 설정 (처음 한 번)

```bash
sudo "/Library/Application Support/VirtioVGA/opencore-setup.sh"
```

스크립트가 하는 일은 다음과 같습니다.

1. `EFI/OC/config.plist`가 있는 EFI 파티션을 찾아 마운트하고, 찾은 경로를 보여 준 뒤 계속할지 묻습니다. 찾지 못하면 `--efi disk0s1`처럼 장치를 지정해 주세요.
2. `config.plist`를 `config.plist.bak-<시각>-virtiovga`로 백업합니다.
3. 보조 kext `VirtioVGANdrvBlocker.kext`를 `EFI/OC/Kexts`에 복사하고 `Kernel > Add`에 항목을 넣습니다. 이 kext는 부팅 초기에 기존 화면 드라이버(IONDRV)가 virtio GPU에 붙지 않게 합니다.
4. `csr-active-config`를 `0x201`(`01020000`, Kext Signing만 해제)로 설정합니다. 서명 없는 본 드라이버를 보조 kernel collection에서 로드하려면 필요합니다.

바꿀 내용만 보려면 `--dry-run`을 붙여 실행합니다. 끝나면 출력 마지막 줄에 되돌리는 명령이 나옵니다. 백업 파일 이름을 적어 두세요.

## 6. 재부팅과 확인

```bash
sudo shutdown -r now
```

부팅 뒤 상태를 확인합니다.

```bash
"/Library/Application Support/VirtioVGA/vvstat"
```

- 종료 코드가 0이고 `init driver-ok`, `framebuffer: attached`가 보이면 정상입니다.
- `system_profiler SPDisplaysDataType`에서 GPU 이름이 **Virtio GPU**, VRAM이 **128 MB**로 보입니다.
- 시스템 설정 > 디스플레이에서 해상도를 고를 수 있습니다(1024×768 ~ 2560×1600, HiDPI 포함).
- `csrutil status`가 Custom Configuration(Kext Signing: disabled)이어야 합니다.

## 7. 사용과 설정

- **창 크기 자동 전환**: 호스트에서 QEMU 창 크기를 바꾸면 게스트 해상도가 따라 바뀝니다. 끄려면 boot-arg `vvideo_autoresize=0`을 넣습니다([INSTALL.md §5.1](INSTALL.md)).
- **설정값**: boot-arg 목록과 kext 기본값은 [INSTALL.md §5](INSTALL.md)에 있습니다.
- **알려진 제약**: Metal(GPU 가속)과 투명 효과는 없습니다. 그 밖의 제약은 [INSTALL.md §8](INSTALL.md)에 있습니다.

## 8. 부팅이 되지 않거나 화면이 이상할 때

가벼운 방법부터 써 주세요.

1. **드라이버만 끄기**: OpenCore 부팅 선택 화면 또는 Recovery 터미널에서 boot-args에 `-vvideooff`를 넣으면 드라이버가 장치에 붙지 않고 기존 화면으로 부팅됩니다. 방법은 [INSTALL.md §7](INSTALL.md)에 있습니다.
2. **config.plist 되돌리기**: Recovery 터미널에서 5단계의 백업 파일을 `config.plist`로 복사합니다.

   ```bash
   diskutil mount disk0s1
   ```

   ```bash
   cp /Volumes/EFI/EFI/OC/config.plist.bak-<시각>-virtiovga /Volumes/EFI/EFI/OC/config.plist
   ```

3. **스냅샷 복원**: 호스트에서 VM을 끄고 2단계의 스냅샷으로 되돌립니다.

   ```bash
   qemu-img snapshot -a before-virtiovga <게스트 디스크 이미지>.qcow2
   ```

문제를 알려 주실 때는 `vvstat --log 40` 출력과 `/Library/Logs/DiagnosticReports`의 패닉 보고서를 함께 보내 주세요([DEBUGGING.md](DEBUGGING.md)).

## 9. 제거

```bash
sudo "/Library/Application Support/VirtioVGA/uninstall.sh"
```

순서대로 OpenCore의 보조 kext를 끄고(백업 후 `Enabled false`), 에이전트와 본 드라이버를 지운 뒤 보조 kernel collection을 다시 만듭니다. 끝나면 재부팅합니다. 보조 kext만 남고 본 드라이버가 없으면 기존 화면 드라이버도 막혀 화면이 나오지 않으므로, 손으로 지우지 말고 이 스크립트를 써 주세요. 제거 스크립트의 보조 kernel collection 재생성 단계는 아직 실제로 실행해 보지 않았습니다.

## 10. 0.9.0 이전 버전에서 올리기

0.9.0에서 이름이 바뀌었습니다(`VirtioVideo.kext` → `VirtioVGA.kext`, `VirtioVideoNdrvBlocker.kext` → `VirtioVGANdrvBlocker.kext`, bundle id `local.virtiovideo.*` → `local.virtiovga.*`). 새 VM과 같은 순서(4~6단계)로 설치하면 됩니다.

- 패키지가 예전 `/Library/Extensions/VirtioVideo.kext`와 예전 에이전트(`local.virtiovideo.vvresize`)를 지웁니다.
- `opencore-setup.sh`가 `Kernel > Add`의 예전 `VirtioVideo*` 항목과 EFI의 예전 kext 폴더를 지우고 새 항목을 넣습니다.
- 개발 중에 `scripts/vvresize-agent.sh install`로 사용자 에이전트를 설치했다면, 패키지가 그것도 지우고 전역 에이전트로 바꿉니다.

이 순서는 0.0.51을 쓰던 VM에서 2026-11-25에 확인했습니다.
