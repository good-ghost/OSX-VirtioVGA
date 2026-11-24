# 설치·비활성화·롤백 절차

대상 환경: OSX-KVM 게스트 macOS 15.x, OpenCore 1.0.6 (`REL-106-2025-11-03`), EFI 파티션 `disk0s1`.
기준 설정값은 2026-11-23의 `EFI/OC/config.plist`에서 읽었다. 설정이 바뀌면 이 문서도 고친다.

kext는 커널에서 돈다. 설치 전에 반드시 §1의 준비를 마친다.

## 1. 준비 (설치할 때마다)

1. **호스트 디스크 스냅샷**을 만든다. VM을 끈 상태에서 실행한다 (켜진 상태의 qcow2에 `qemu-img snapshot`을 쓰면 이미지가 손상될 수 있다).

   ```bash
   qemu-img snapshot -c before-virtiovideo-<날짜> <게스트 디스크 이미지 경로>.qcow2
   ```

   목록 확인과 복원:

   ```bash
   qemu-img snapshot -l <게스트 디스크 이미지 경로>.qcow2
   ```

   ```bash
   qemu-img snapshot -a before-virtiovideo-<날짜> <게스트 디스크 이미지 경로>.qcow2
   ```

   게스트 디스크가 qcow2가 아니면 이미지 파일을 복사해 둔다.

2. 빌드: `scripts/build.sh` (종료 코드 0), `kmutil print-diagnostics -p build/VirtioVideo.kext`에서 `Dependencies: OK`.
3. 설치할 빌드의 dSYM이 `build/dsym-archive/<UUID>.dSYM`에 있는지 확인한다 (패닉 분석에 필요, [DEBUGGING.md §2.3](DEBUGGING.md)). UUID: `dwarfdump --uuid build/VirtioVideo.kext/Contents/MacOS/VirtioVideo`

## 2. EFI 마운트

관리자 비밀번호가 필요하다. 사용자가 터미널에서 직접 실행한다.

```bash
sudo diskutil mount disk0s1
```

마운트 위치: `/Volumes/EFI`. 재부팅하면 풀린다.

## 3. 설치

```bash
cp -p /Volumes/EFI/EFI/OC/config.plist /Volumes/EFI/EFI/OC/config.plist.bak-<날짜>-<WI>
```

```bash
rm -rf /Volumes/EFI/EFI/OC/Kexts/VirtioVideo.kext && ditto --norsrc --noextattr build/VirtioVideo.kext /Volumes/EFI/EFI/OC/Kexts/VirtioVideo.kext && dot_clean -m /Volumes/EFI/EFI/OC/Kexts/VirtioVideo.kext
```

```bash
diff -r build/VirtioVideo.kext /Volumes/EFI/EFI/OC/Kexts/VirtioVideo.kext && echo identical
```

`config.plist > Kernel > Add`의 항목 (처음 한 번만 추가. 현재 인덱스 8, Lilu·WhateverGreen보다 뒤):

| 키             | 값                              |
| -------------- | ------------------------------- |
| Arch           | `x86_64`                        |
| BundlePath     | `VirtioVideo.kext`              |
| Comment        | `VirtioVideo`                   |
| Enabled        | `true`                          |
| ExecutablePath | `Contents/MacOS/VirtioVideo`    |
| MaxKernel      | (빈 값)                         |
| MinKernel      | `24.0.0`                        |
| PlistPath      | `Contents/Info.plist`           |

수정 후 `plutil -lint /Volumes/EFI/EFI/OC/config.plist`가 `OK`여야 한다. 그다음 재부팅한다.

설치 확인:

```bash
kmutil showloaded | grep -i virtiovideo
```

```bash
ioreg -r -c VirtioVideoProbe -l -w0 -d1 | grep '"VV'
```

## 4. 드라이버를 끄는 방법

가벼운 것부터 쓴다.

### 4.1 boot-arg `-vvideooff` (kext는 로드되지만 장치에 붙지 않는다)

boot-args는 OpenCore가 **부팅할 때마다 config.plist 값으로 다시 쓴다** (`NVRAM > Delete`에 `boot-args`가 있음). 그래서 macOS나 Recovery에서 `nvram boot-args=...`로 바꿔도 다음 부팅에 덮어써진다. config.plist를 고친다.

- 위치: `NVRAM > Add > 7C436110-AB2A-4BBB-A880-FE41995C9F82 > boot-args`
- 기본값: `keepsyms=1`
- 끌 때: `keepsyms=1 -vvideooff`

```bash
/usr/libexec/PlistBuddy -c "Set :NVRAM:Add:7C436110-AB2A-4BBB-A880-FE41995C9F82:boot-args keepsyms=1 -vvideooff" /Volumes/EFI/EFI/OC/config.plist
```

확인: 재부팅 후 `ioreg -r -c VirtioVideoProbe`에 객체가 없다. (`kmutil showloaded`에는 kext가 계속 보일 수 있다.)

로그 수준을 바꿀 때도 같은 위치에 `vvideo_log=0|1|2`를 넣는다.

### 4.2 `Enabled=false` (kext를 아예 로드하지 않는다)

```bash
/usr/libexec/PlistBuddy -c "Set :Kernel:Add:8:Enabled false" /Volumes/EFI/EFI/OC/config.plist
```

인덱스 8이 `VirtioVideo.kext`인지 먼저 확인한다:

```bash
/usr/libexec/PlistBuddy -c "Print :Kernel:Add:8:BundlePath" /Volumes/EFI/EFI/OC/config.plist
```

확인: 재부팅 후 `kmutil showloaded | grep -i virtiovideo`가 비어 있다.

## 5. 부팅이 안 될 때

### 5.1 OpenCore 부팅 선택 화면에서 Recovery로 부팅

현재 설정: `ShowPicker=true`, `Timeout=2`(초), `HideAuxiliary=true`, `PickerMode=External`.

1. VM을 켜고 2초 안에 부팅 선택 화면에서 방향키를 눌러 자동 부팅을 멈춘다.
2. `HideAuxiliary=true`라 Recovery가 숨겨져 있다. **Space** 키를 눌러 보조 항목을 표시하고 macOS Recovery를 고른다.
3. Recovery의 유틸리티 > 터미널에서 (Recovery 터미널은 root라 비밀번호가 필요 없다):

   ```bash
   diskutil mount disk0s1
   ```

   ```bash
   ls /Volumes/EFI/EFI/OC/config.plist.bak-*
   ```

   ```bash
   cp /Volumes/EFI/EFI/OC/config.plist.bak-<가장 최근 정상 백업> /Volumes/EFI/EFI/OC/config.plist
   ```

4. 재부팅한다.

Recovery에서도 kext가 주입되므로 Recovery마저 부팅되지 않으면 §5.2로 간다.

### 5.2 호스트 스냅샷 복원

VM을 끄고 §1의 `qemu-img snapshot -a`로 되돌린다. EFI 파티션도 같은 디스크(`disk0s1`)에 있으므로 함께 되돌아간다.

### 5.3 부팅은 되는데 화면만 검을 때 (호스트에서 SSH로 복구)

드라이버가 virtio-gpu에 명령을 보내기 시작하면 QEMU가 화면 출력을 VGA에서 virtio-gpu로 바꿀 수 있다. 우리가 scanout을 설정하기 전(P2 이전)에는 그때 화면이 검게 보인다. macOS는 계속 돌고 있으므로 원격 로그인(SSH)으로 복구한다. 게스트의 원격 로그인이 켜져 있어야 한다 (2026-11-24 켜져 있음, 게스트 IP `192.168.122.75`).

호스트에서:

```bash
ssh woomg@192.168.122.75
```

게스트 셸에서 (비밀번호는 사용자가 입력):

```bash
sudo diskutil mount disk0s1
```

```bash
/usr/libexec/PlistBuddy -c "Set :Kernel:Add:8:Enabled false" /Volumes/EFI/EFI/OC/config.plist && plutil -lint /Volumes/EFI/EFI/OC/config.plist
```

```bash
sudo shutdown -r now
```

화면이 검어도 Claude 데스크톱 세션은 게스트 안에서 계속 돈다. 다른 기기에서 Remote Control로 이어서 진단할 수 있다.

## 6. 백업 파일 목록

| 파일 (EFI/OC/)                        | 만든 시점                         | 내용                                       |
| ------------------------------------- | --------------------------------- | ------------------------------------------ |
| `config.plist.bak-20261123-wi0002`    | WI-0.002 첫 설치 전               | VirtioVideo 항목이 없는 원래 설정          |
| `config.plist.bak-20261123-wi0005`    | WI-0.005 boot-args 변경 전        | VirtioVideo 항목 있음, boot-args `keepsyms=1` |
| `config.plist.bak-20261123-wi0003`    | WI-0.003 시험 전                  | VirtioVideo 항목 있음(Enabled=true), boot-args `keepsyms=1 vvideo_log=0` |
| `config.plist.bak-20261123-wi0004`    | WI-0.004 kext 재활성화 전         | VirtioVideo 항목 있음(Enabled=false), boot-args `keepsyms=1` |
| `config.plist.bak-20261124-wi1001`    | WI-1.001 시험 전                  | Enabled=true, boot-args `keepsyms=1` |
| `config.plist.bak-20261124-wi1007`    | WI-1.007 Poll 시험 전             | Enabled=true, boot-args `keepsyms=1` |
| `config.plist.bak-20261124-wi1006`    | WI-1.006 복구 시험 전             | Enabled=true, boot-args `keepsyms=1 vvideo_irq=2` |

## 7. 현재 상태 (2026-11-23)

- boot-args: `keepsyms=1`
- `Kernel > Add:8` VirtioVideo: `Enabled=true` (WI-0.004에서 다시 켬)
- EFI의 kext 버전: 0.0.18, UUID `31654AC5-A594-3B14-8F2B-CCA02D0185BF` (dSYM은 `build/dsym-archive/`). 시작 때 선호 모드 프레임버퍼(물리 연속)를 할당해 둔다. `VirtioGpuDevice`가 GPU 장치를 DRIVER_OK(0x0F)까지 초기화하고 `GET_DISPLAY_INFO`를 주고받으며, 장치가 reset되면 스스로 복구한다. 디버그 빌드는 시작 때 오류 응답 자체 시험(`VVSelfTest`)을 한다. 화면은 IONDRV가 그린다
- boot-args `keepsyms=1` (기본값으로 되돌림, 2026-11-24)
- 디버그 boot-arg: `vvideo_log=0|1|2`(로그 수준), `vvideo_irq=0|1|2`(MSI-X/INTx/Poll), `vvideo_test_reset=1`(30초 뒤 외부 reset 흉내), `-vvideooff`(드라이버 끄기)
- boot-arg `vvideo_irq=0|1|2`로 인터럽트 방식을 고를 수 있다 (이 장치에서 1은 Poll로 내려간다)
- QEMU: `-gdb tcp:127.0.0.1:1234` 사용 중
