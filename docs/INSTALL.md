# 설치·설정·롤백 절차

[English](INSTALL.en.md) | 한국어

대상 환경: OSX-KVM 게스트 macOS 15.x (확인: 15.8.3 `24H241`), OpenCore 1.0.6, EFI 파티션 `disk0s1`, QEMU `-device virtio-vga`.
2026-11-25 기준으로 다시 썼습니다(WI-5.009). 설정이 바뀌면 이 문서도 고칩니다.

kext는 커널에서 돕니다. 설치·갱신할 때마다 §1의 준비를 먼저 해 주세요.

**새 VM에 설치할 때는 [NEW-VM-INSTALL.md](NEW-VM-INSTALL.md)(pkg)를 따라 주세요.** 이 문서는 소스에서 빌드해 개별 파일을 손으로 설치·교체하는 개발용 절차와 설정·롤백의 상세를 다룹니다. 0.9.0에서 이름이 바뀌었습니다(`VirtioVideo` → `VirtioVGA`, bundle id `local.virtiovideo.*` → `local.virtiovga.*`).

## 구성 한눈에 보기

| 구성 요소 | 위치 | 로드 방식 | 역할 |
| --------- | ---- | --------- | ---- |
| `VirtioVGA.kext` (본 드라이버) | `/Library/Extensions` | 보조 kernel collection (`kmutil load -z -p` 후 재부팅) | virtio-gpu 장치 초기화, `IOFramebuffer`, 화면 전송, 커서, EDID |
| `VirtioVGANdrvBlocker.kext` (보조) | `EFI/OC/Kexts` | OpenCore 주입 (Boot KC) | 부팅 초기에 GPU PCI 장치에 `AAPL,iokit-ignore-ndrv`를 붙여 IONDRV가 붙지 않게 합니다 |
| `vvresize` | pkg: `/Library/Application Support/VirtioVGA` + `/Library/LaunchAgents`, 개발용: `~/Library/Application Support/VirtioVGA` (`scripts/vvresize-agent.sh install`) | 로그인 때 시작 | 호스트 창 크기 자동 전환을 마무리합니다 (§5.4). 둘 중 하나만 둡니다 |
| 도구 (pkg) | `/Library/Application Support/VirtioVGA` | — | `opencore-setup.sh`(OpenCore 설정), `uninstall.sh`(제거), `vvstat`(상태 조회) |

- 본 드라이버를 OpenCore로 주입하지 않는 이유: IOGraphicsFamily 심볼을 쓰는 kext는 OpenCore 주입으로 로드되지 않습니다(T-04).
- SIP: `csr-active-config` = `01020000`(0x201, Kext Signing만 해제). 이보다 더 풀지 않습니다.
- kext 교체는 **재부팅으로** 합니다. 화면을 맡은 동안 `kmutil unload`를 하면 VM이 패닉 상태로 멈춥니다(NFR-003 v1.3, WI-5.003).

## 1. 준비 (설치·갱신할 때마다)

1. **호스트 디스크 스냅샷**을 만들어 주세요. VM을 끈 상태에서 실행합니다 (켜진 상태의 qcow2에 `qemu-img snapshot`을 쓰면 이미지가 손상될 수 있습니다).

   ```bash
   qemu-img snapshot -c before-virtiovga-<날짜> <게스트 디스크 이미지 경로>.qcow2
   ```

   목록 확인과 복원:

   ```bash
   qemu-img snapshot -l <게스트 디스크 이미지 경로>.qcow2
   ```

   ```bash
   qemu-img snapshot -a before-virtiovga-<날짜> <게스트 디스크 이미지 경로>.qcow2
   ```

   게스트 디스크가 qcow2가 아니면 이미지 파일을 복사해 둡니다.

2. 빌드와 시험: `scripts/build.sh`, `scripts/test.sh` (둘 다 종료 코드 0), `kmutil libraries -p build/VirtioVGA.kext` (종료 코드 0).
3. 설치할 빌드의 dSYM이 `build/dsym-archive/<UUID>.dSYM`에 있는지 확인해 주세요 (패닉 분석에 필요, [DEBUGGING.md §2.3](DEBUGGING.md)). UUID: `dwarfdump --uuid build/VirtioVGA.kext/Contents/MacOS/VirtioVGA`

## 2. 처음 한 번만 하는 설정 (OpenCore)

EFI를 마운트합니다 (관리자 비밀번호, 사용자가 직접 입력). 마운트 위치 `/Volumes/EFI`, 재부팅하면 풀립니다.

```bash
sudo diskutil mount disk0s1
```

고치기 전에 백업해 주세요 (§9 목록에 적습니다).

```bash
sudo cp -p /Volumes/EFI/EFI/OC/config.plist /Volumes/EFI/EFI/OC/config.plist.bak-<날짜>-<사유>
```

`packaging/opencore-setup.sh`(pkg에서는 `/Library/Application Support/VirtioVGA/opencore-setup.sh`)가 아래 설정과 보조 kext 복사를 백업 후 한 번에 합니다. 손으로 할 때는 `config.plist`에 다음이 있어야 합니다.

| 위치 | 값 |
| ---- | -- |
| `NVRAM > Add > 7C436110-AB2A-4BBB-A880-FE41995C9F82 > csr-active-config` | `01020000` (data) |
| `NVRAM > Delete > 7C436110-…` | `csr-active-config`, `boot-args` 포함 |
| `NVRAM > Add > 7C436110-… > boot-args` | `keepsyms=1` (기본) |
| `Kernel > Add` VirtioVGANdrvBlocker | `Arch x86_64`, `BundlePath VirtioVGANdrvBlocker.kext`, `ExecutablePath Contents/MacOS/VirtioVGANdrvBlocker`, `PlistPath Contents/Info.plist`, `MinKernel 24.0.0`, `Enabled true`. Lilu·WhateverGreen보다 뒤 |
| `Kernel > Add` VirtioVGA (예전 OpenCore 주입 항목) | 있으면 `Enabled false` (2026-11-24부터) |

- data 값은 PlistBuddy로 넣으면 ASCII 문자열로 들어갑니다. `plutil -replace … -data AQIAAA==`(= `01020000`)를 씁니다.
- 배열에 항목을 넣을 때는 PlistBuddy 대신 `plutil -insert Kernel.Add -json '{…}' -append`를 씁니다 (PlistBuddy로 넣으면 위치가 어긋났습니다).
- 수정 후 `plutil -lint /Volumes/EFI/EFI/OC/config.plist`가 `OK`여야 합니다.

보조 kext 복사 (처음, 그리고 보조 kext를 바꿀 때):

```bash
sudo rm -rf /Volumes/EFI/EFI/OC/Kexts/VirtioVGANdrvBlocker.kext && sudo ditto --norsrc --noextattr build/VirtioVGANdrvBlocker.kext /Volumes/EFI/EFI/OC/Kexts/VirtioVGANdrvBlocker.kext && sudo dot_clean -m /Volumes/EFI/EFI/OC
```

재부팅 후 `csrutil status`가 Custom Configuration(Kext Signing: disabled)인지 봐 주세요.

## 3. 본 드라이버 설치·갱신

사용자가 터미널에서 비밀번호를 입력합니다.

```bash
sudo rm -rf /Library/Extensions/VirtioVGA.kext && sudo ditto --norsrc --noextattr build/VirtioVGA.kext /Library/Extensions/VirtioVGA.kext && sudo chown -R root:wheel /Library/Extensions/VirtioVGA.kext && sudo kmutil load -z -p /Library/Extensions/VirtioVGA.kext
```

- 정상 결과는 Code 28 "requires a reboot"입니다. 그다음 재부팅해 주세요.
- `-z`(승인 검사 건너뛰기)가 없으면 Code 27(not approved)로 실패합니다. 서명 없는 kext는 시스템 설정의 허용 버튼으로 승인되지 않았습니다.
- 재부팅 전 확인(선택): `kmutil inspect -B /Library/KernelCollections/AuxiliaryKernelExtensions.kc | grep virtiovga`

## 4. 설치 확인

```bash
scripts/vvstat.sh
```

- 종료 코드 0이면 드라이버가 장치를 초기화했고(`init driver-ok`) 프레임버퍼가 붙어 있습니다.
- 첫 줄에서 kext 버전을, `--log 20`으로 최근 로그를 봅니다. 자세한 해석은 [DEBUGGING.md §1](DEBUGGING.md).
- 로드 위치: `kmutil showloaded --collection aux | grep -i virtiovga` (본 드라이버), `kmutil showloaded --collection boot | grep -i virtiovga` (보조 kext)
- IONDRV 차단: `ioreg -r -c IOPCIDevice -l -w0 | grep VVNdrvBlocked` → `"set"`

## 5. 설정

### 5.1 boot-args

boot-args는 OpenCore가 **부팅할 때마다 config.plist 값으로 다시 씁니다** (`NVRAM > Delete`에 `boot-args`가 있음). macOS나 Recovery에서 `nvram boot-args=...`로 바꿔도 다음 부팅에 덮어써지므로 config.plist를 고쳐 주세요.

```bash
sudo /usr/libexec/PlistBuddy -c "Set :NVRAM:Add:7C436110-AB2A-4BBB-A880-FE41995C9F82:boot-args keepsyms=1 <추가할 값>" /Volumes/EFI/EFI/OC/config.plist
```

| boot-arg | 값 | 기본 | 용도 |
| -------- | -- | ---- | ---- |
| `-vvideooff` | 플래그 | 없음 | 드라이버 끄기. 보조 kext가 속성을 붙이지 않고 본 kext도 매칭을 거절해 IONDRV가 화면을 맡습니다 (§6) |
| `-vvideoswcursor` | 플래그 | 없음 | 하드웨어 커서를 끄고 소프트웨어 커서 사용 (REQ-051) |
| `vvideo_cursor_scale` | 50 / 100 | Info.plist 값 | 커서 크기 비율 (§5.2) |
| `vvideo_flush_ms` | 4~1000 | 16 | 화면 전송 주기(ms). 범위 밖이면 기본값 |
| `vvideo_dirty` | 0 / 1 | Info.plist 값 | 바뀐 영역만 전송 (§5.2) |
| `vvideo_dirty_tile` | 16~256의 2의 거듭제곱 | Info.plist 값 | 변경 감지 타일 크기 |
| `vvideo_autoresize` | 0 / 1 | Info.plist 값 | 호스트 창 크기 자동 전환 (§5.4) |
| `vvideo_log` | 0 / 1 / 2 | 1 | 로그 수준 (0 오류, 1 정보, 2 디버그) |
| `vvideo_irq` | 0 / 1 / 2 | 0 | 인터럽트 방식 (MSI-X / INTx / Poll). 이 장치에서 1은 Poll로 내려갑니다 |
| `vvideo_fail` | 1 / 2 / 3 | 없음 | 시험용: 전송 계층·장치 초기화·프레임버퍼 단계 실패 주입 (IONDRV로 돌아가는지 확인) |
| `vvideo_test_reset` | 1 | 없음 | 시험용: 30초 뒤 장치 reset 흉내 |
| `vvideo_test_pattern` | 1 | 없음 | 시험용: 90초 뒤 색 띠 15초 |
| `vvideo_takeover` | 1 | 없음 | 시험용: 실행 중 IONDRV 넘겨받기. **패닉이 날 수 있습니다** |

### 5.2 kext Info.plist 기본값

EFI를 고치지 않고 kext 설치만으로 정하는 기본값입니다. `kext/Info.plist`의 `IOKitPersonalities > VirtioGpuDevice`에 있습니다. 바꾸면 다시 빌드·설치해 주세요. 같은 이름의 boot-arg가 있으면 boot-arg가 이깁니다.

| 키 | 기본 | 의미 |
| -- | ---- | ---- |
| `VVCursorScale` | 100 | 하드웨어 커서 크기(%). QEMU GTK는 커서를 창 배율 없이 그립니다. HiDPI 게스트(2560×1600)를 호스트 창 1280×800처럼 절반으로 줄여 볼 때는 50으로 맞춥니다(boot-arg `vvideo_cursor_scale=50`). 0.9.1부터 기본값 100 (WI-4.001) |
| `VVDirtyFlush` | 0 | 바뀐 영역만 전송. 측정 결과 호스트 이득이 없고 게스트 CPU만 늘어 꺼 둡니다 |
| `VVDirtyTile` | 64 | 변경 감지 타일 크기 |
| `VVAutoResize` | 1 | 호스트 창 크기 자동 전환 (§5.4). 0이면 창 크기가 바뀌어도 모드 목록을 바꾸지 않습니다 |

### 5.3 실행 중 바꾸기 (측정용)

`VVDirtyFlush`·`VVDirtyTile`만 재부팅 없이 바꿀 수 있습니다. root가 필요합니다.

```bash
scripts/vvctl.sh dirty 1
```

### 5.4 호스트 창 크기 자동 전환 (WI-6.001)

호스트에서 QEMU 창 크기를 바꾸면 게스트 해상도가 그 크기로 바뀝니다. 드라이버와 에이전트 두 부분으로 됩니다.

1. 드라이버: 창 크기 이벤트를 받으면 그 크기를 동적 모드(ID 100)로 넣고, 이벤트가 600 ms 멎으면 IOGraphics에 연결 변경을 알립니다. WindowServer가 모드 목록을 다시 읽고 EDID 기본 해상도(= 창 크기)인 ID 100으로 바꿉니다.
2. 에이전트(`vvresize`): WindowServer는 그 뒤 디스플레이 설정에 저장된 모드로 돌아갑니다. 저장된 모드가 표준 모드(예: 시스템 설정에서 고른 1920×1080)면 에이전트가 ID 100으로 다시 바꾸고 설정에 저장합니다. 한 번 저장되면 그다음부터는 WindowServer만으로 바뀝니다.

설치·확인·제거 (sudo 필요 없음, 로그 `~/Library/Logs/vvresize.log`):

```bash
scripts/vvresize-agent.sh install
```

```bash
scripts/vvresize-agent.sh status
```

```bash
scripts/vvresize-agent.sh uninstall
```

- 끄기: boot-arg `vvideo_autoresize=0` 또는 Info.plist `VVAutoResize` 0. 에이전트는 드라이버 설정을 따라 아무것도 하지 않습니다.
- 창 크기를 바꿀 때 화면이 한 번 깜박입니다. 해상도가 바뀌기까지 약 1초 걸립니다(저장된 모드가 표준 모드면 약 3초, 깜박임 2번).
- 에이전트는 바꾼 모드를 디스플레이 설정에 저장합니다. 시스템 설정에서 해상도를 고르면 그 값이 다시 저장됩니다.
- 상태: `scripts/vvstat.sh`의 `auto resize` 줄 (`on handler=enabled connect-changes=N last=WxH`).

## 6. 드라이버를 끄거나 빼는 방법

가벼운 것부터 써 주세요. pkg로 설치했다면 완전히 빼는 것은 `sudo "/Library/Application Support/VirtioVGA/uninstall.sh"`로 합니다(보조 kext를 먼저 끈 뒤 본 kext를 지웁니다). 보조 kext만 남고 본 kext가 없으면 IONDRV도 막혀 화면이 나오지 않습니다.

1. **boot-arg `-vvideooff`** (§5.1): kext는 로드되지만 장치에 붙지 않습니다. 확인: `scripts/vvstat.sh`가 프레임버퍼 `not attached` 또는 `VirtioGpuDevice: not found`.
2. **보조 kext 끄기**: `Kernel > Add`의 VirtioVGANdrvBlocker `Enabled false`. 본 kext는 IONDRV가 먼저 붙어 있으면 프레임버퍼로 매칭하지 않습니다.
3. **본 kext 빼기**: `sudo rm -rf /Library/Extensions/VirtioVGA.kext` 후 `sudo kmutil rebuild`, 재부팅. (이 절차는 아직 실행해 보지 않았습니다.)

## 7. 부팅이 안 될 때

부팅은 되는데 화면이 이상하면 §7.3, 부팅 자체가 안 되면 §7.1부터.

### 7.1 OpenCore 부팅 선택 화면에서 Recovery로 부팅

현재 설정: `ShowPicker=true`, `Timeout=2`(초), `HideAuxiliary=true`, `PickerMode=External`.

1. VM을 켜고 2초 안에 부팅 선택 화면에서 방향키를 눌러 자동 부팅을 멈춰 주세요.
2. `HideAuxiliary=true`라 Recovery가 숨겨져 있습니다. **Space** 키를 눌러 보조 항목을 표시하고 macOS Recovery를 골라 주세요.
3. Recovery의 유틸리티 > 터미널에서 (Recovery 터미널은 root라 비밀번호가 필요 없습니다):

   ```bash
   diskutil mount disk0s1
   ```

   ```bash
   ls /Volumes/EFI/EFI/OC/config.plist.bak-*
   ```

   ```bash
   cp /Volumes/EFI/EFI/OC/config.plist.bak-<가장 최근 정상 백업> /Volumes/EFI/EFI/OC/config.plist
   ```

4. 재부팅해 주세요.

백업으로 되돌리는 대신 같은 터미널에서 boot-args에 `-vvideooff`를 넣어도 됩니다(§5.1의 PlistBuddy 명령, Recovery에서는 `sudo` 없이).

본 kext 자체를 빼야 하면: 안전 모드(boot-arg `-x` 또는 부팅 때 Shift)로 들어가 `sudo rm -rf /Library/Extensions/VirtioVGA.kext` 후 `sudo kmutil rebuild`, 그래도 안 되면 Recovery에서 `kmutil trigger-panic-medic --volume-root /Volumes/<볼륨>`. (둘 다 아직 실행해 보지 않았습니다.)

Recovery에서도 OpenCore가 보조 kext(NdrvBlocker)를 주입합니다. Recovery마저 부팅되지 않으면 §7.2로 갑니다.

### 7.2 호스트 스냅샷 복원

VM을 끄고 §1의 `qemu-img snapshot -a`로 되돌려 주세요. EFI 파티션도 같은 디스크(`disk0s1`)에 있으므로 함께 되돌아갑니다.

### 7.3 부팅은 되는데 화면이 이상할 때 (호스트에서 SSH로 복구)

macOS는 계속 돌고 있으므로 원격 로그인(SSH)으로 `-vvideooff`를 넣습니다. 게스트의 원격 로그인이 켜져 있어야 합니다 (2026-11-24 켜져 있음, 게스트 IP `192.168.122.75`).

```bash
ssh <user>@192.168.122.75
```

게스트 셸에서 (비밀번호는 사용자가 입력):

```bash
sudo diskutil mount disk0s1
```

```bash
sudo /usr/libexec/PlistBuddy -c "Set :NVRAM:Add:7C436110-AB2A-4BBB-A880-FE41995C9F82:boot-args keepsyms=1 -vvideooff" /Volumes/EFI/EFI/OC/config.plist && plutil -lint /Volumes/EFI/EFI/OC/config.plist
```

```bash
sudo shutdown -r now
```

화면이 멈춰도 Claude 데스크톱 세션은 게스트 안에서 계속 돕니다. 다른 기기에서 Remote Control로 이어서 진단할 수 있습니다.

## 8. 알려진 제약

| 항목 | 내용 |
| ---- | ---- |
| 실행 중 언로드 | 지원하지 않습니다. 화면을 맡은 동안 `kmutil unload`를 하면 VM이 패닉 상태로 멈춥니다 (WI-5.003) |
| 시스템 잠자기 | 사용하지 않는 설정(`pmset -a sleep 0`)을 권합니다. QEMU VM은 S3에서 키 입력으로 깨어나지 않습니다. 잠들기는 패닉 없이 되지만 깨어나기는 시험하지 못했습니다 (WI-4.003) |
| 디스플레이 잠자기 | 지원. 잠자기 동안 호스트 창에 "Display output is not active."가 보이고 키 입력으로 돌아옵니다 (WI-4.002) |
| Metal·투명 효과 | 없습니다. 2D 프레임버퍼 드라이버라 Metal 장치가 생기지 않습니다. Sequoia 같은 Metal 배경은 단색으로 대체되고 메뉴 막대·Dock은 불투명합니다 (Q-12, WI-5.007) |
| 녹화 | 게스트 `screencapture -V`가 실패합니다. 화면 녹화는 호스트에서 합니다 |
| 창 최소화 애니메이션 | 2560×1600 HiDPI에서 최소화(Genie)가 3~10초 걸리고 끊겨 보입니다. Metal이 없어 WindowServer가 CPU로 그리기 때문입니다. 1280×800 1:1에서는 IONDRV와 비슷한 1~3초. 우회: 1:1 모드 또는 Dock 최소화 효과 "크기 조절" |

## 9. config.plist 백업 목록

| 파일 (EFI/OC/)                        | 만든 시점                         | 내용                                       |
| ------------------------------------- | --------------------------------- | ------------------------------------------ |
| `config.plist.bak-20261123-wi0002`    | WI-0.002 첫 설치 전               | VirtioVGA 항목이 없는 원래 설정          |
| `config.plist.bak-20261123-wi0005`    | WI-0.005 boot-args 변경 전        | VirtioVGA 항목 있음, boot-args `keepsyms=1` |
| `config.plist.bak-20261123-wi0003`    | WI-0.003 시험 전                  | VirtioVGA 항목 있음(Enabled=true), boot-args `keepsyms=1 vvideo_log=0` |
| `config.plist.bak-20261123-wi0004`    | WI-0.004 kext 재활성화 전         | VirtioVGA 항목 있음(Enabled=false), boot-args `keepsyms=1` |
| `config.plist.bak-20261124-wi1001`    | WI-1.001 시험 전                  | Enabled=true, boot-args `keepsyms=1` |
| `config.plist.bak-20261124-wi1007`    | WI-1.007 Poll 시험 전             | Enabled=true, boot-args `keepsyms=1` |
| `config.plist.bak-20261124-wi1006`    | WI-1.006 복구 시험 전             | Enabled=true, boot-args `keepsyms=1 vvideo_irq=2` |
| `config.plist.bak-20261124-wi2004`    | WI-2.004 패턴 시험 전             | Enabled=true, boot-args `keepsyms=1` |
| `config.plist.bak-20261124-wi2005-t04` | T-04 스텁(0.0.21) OC 주입 시험 전 | Enabled=true, boot-args `keepsyms=1` |
| `config.plist.bak-20261124-auxkc-sip` | SIP 0x201·OC 주입 끄기 전 | Enabled=true, csr `00000000` |
| `config.plist.bak-20261124-wi2004-redo` | 스냅샷 복구 후 WI-2.004 재설치 전 | VirtioVGA 항목이 없는 원래 설정 (복구로 위 백업들은 EFI에서 사라짐) |
| `config.plist.bak-20261125-wi4001`    | WI-4.001 소프트웨어 커서 시험 전  | boot-args `keepsyms=1` |
| `config.plist.bak-20261125-minimize` | 최소화 IONDRV 비교 전             | boot-args `keepsyms=1` |
| `config.plist.bak-20261125-wi5010`   | WI-5.010 AC-05·06 재부팅 시험 전  | boot-args `keepsyms=1` |
| `config.plist.bak-20261125-wi6001`   | WI-6.001 자동 전환 끄기 시험 전   | boot-args `keepsyms=1` |
| `config.plist.bak-20261125-200809-virtiovga` | 0.9.0 이름 변경, `opencore-setup.sh` 첫 실행 전 | 예전 VirtioVideo·VirtioVideoNdrvBlocker 항목 있음 |
| `config.plist.bak-20261125-201204-virtiovga` | `opencore-setup.sh` 재실행(업그레이드 시험) 전 | VirtioVGANdrvBlocker 항목 있음 |

위 표에서 `wi2004-redo`를 뺀 백업은 2026-11-24 호스트 스냅샷 "WI-001" 복구로 EFI에서 사라졌습니다.

