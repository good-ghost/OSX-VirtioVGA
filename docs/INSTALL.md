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

## 3A. 설치 — 보조 kernel collection (2026-11-24부터 기본, Q-01 v1.2)

IOFramebuffer 하위 클래스는 OpenCore 주입으로 로드되지 않는다(T-04). 그래서 `/Library/Extensions`에 설치한다.

전제 (한 번만): OpenCore `NVRAM > Add > 7C436110-… > csr-active-config` = `01020000`, `NVRAM > Delete > 7C436110-…`에 `csr-active-config`, `Kernel > Add`의 VirtioVideo `Enabled=false`. 재부팅 후 `csrutil status`에서 Kext Signing: disabled 확인.

설치·갱신 (사용자가 터미널에서 비밀번호 입력):

```bash
sudo rm -rf /Library/Extensions/VirtioVideo.kext && sudo ditto --norsrc --noextattr build/VirtioVideo.kext /Library/Extensions/VirtioVideo.kext && sudo chown -R root:wheel /Library/Extensions/VirtioVideo.kext && sudo kmutil load -z -p /Library/Extensions/VirtioVideo.kext
```

- `-z`(승인 검사 건너뛰기)가 없으면 Code 27(not approved)로 실패한다. 서명 없는 kext는 시스템 설정의 허용 버튼으로 승인되지 않았다.
- 정상 결과는 Code 28 "requires a reboot"이다. `kmutil inspect -B /Library/KernelCollections/AuxiliaryKernelExtensions.kc | grep virtiovideo`로 새 버전이 들어갔는지 본 뒤 재부팅한다.
- 확인: `kmutil showloaded --collection aux | grep -i virtiovideo`

되돌리기: §4의 `-vvideooff`, 부팅이 안 되면 안전 모드(boot-arg `-x` 또는 부팅 시 Shift)로 들어가 `sudo rm -rf /Library/Extensions/VirtioVideo.kext` 후 `sudo kmutil rebuild`, 그래도 안 되면 Recovery에서 `kmutil trigger-panic-medic --volume-root /Volumes/<볼륨>`. SIP 원복은 `csr-active-config`=`00000000`.

### 3B. IONDRV 차단 보조 kext (OpenCore 주입, 2026-11-24)

본 kext가 보조 collection으로 늦게 로드되므로, 부팅 초기에 IONDRV가 붙지 못하게 하는 보조 kext를 OpenCore로 넣는다 (OpenCore DeviceProperties는 이 VM에서 장치에 적용되지 않았다, PHASE-PLAN-02).

```bash
rm -rf /Volumes/EFI/EFI/OC/Kexts/VirtioVideoNdrvBlocker.kext && ditto --norsrc --noextattr build/VirtioVideoNdrvBlocker.kext /Volumes/EFI/EFI/OC/Kexts/VirtioVideoNdrvBlocker.kext && dot_clean -m /Volumes/EFI/EFI/OC
```

`Kernel > Add` 항목 (처음 한 번): `Arch x86_64`, `BundlePath VirtioVideoNdrvBlocker.kext`, `ExecutablePath Contents/MacOS/VirtioVideoNdrvBlocker`, `PlistPath Contents/Info.plist`, `MinKernel 24.0.0`, `Enabled true`. PlistBuddy로 배열에 넣으면 위치가 어긋날 수 있어 `plutil -insert Kernel.Add -json '{…}' -append`를 쓴다.

확인: `ioreg -r -c IOPCIDevice -l -w0 | grep VVNdrvBlocked` → `"set"`, `ioreg -r -c IOFramebuffer -w0 -d1`에 `VirtioVideoFramebuffer`만.

끄기: boot-args에 `-vvideooff`를 넣으면 보조 kext가 속성을 붙이지 않아 IONDRV가 평소대로 화면을 맡는다 (본 kext도 매칭을 거절한다).

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

하드웨어 커서를 끄려면 같은 위치에 `-vvideoswcursor`를 넣는다 (소프트웨어 커서, REQ-051). 커서 크기 비율은 kext `Info.plist`의 `VVCursorScale`(기본 50: 호스트 창이 HiDPI 게스트 화면을 절반으로 보여 줄 때)이고, boot-arg `vvideo_cursor_scale=100|50`이 있으면 그것을 쓴다.

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
| `config.plist.bak-20261124-wi2004`    | WI-2.004 패턴 시험 전             | Enabled=true, boot-args `keepsyms=1` |
| `config.plist.bak-20261124-wi2005-t04` | T-04 스텁(0.0.21) OC 주입 시험 전 | Enabled=true, boot-args `keepsyms=1` |
| `config.plist.bak-20261124-auxkc-sip` | SIP 0x201·OC 주입 끄기 전 | Enabled=true, csr `00000000` |
| `config.plist.bak-20261124-wi2004-redo` | 스냅샷 복구 후 WI-2.004 재설치 전 | VirtioVideo 항목이 없는 원래 설정 (복구로 위 백업들은 EFI에서 사라짐) |
| `config.plist.bak-20261125-wi4001`    | WI-4.001 소프트웨어 커서 시험 전  | boot-args `keepsyms=1` |

위 표에서 `wi2004-redo`를 뺀 백업은 2026-11-24 호스트 스냅샷 "WI-001" 복구로 EFI에서 사라졌다 ([WI-2.004.md](WI-2.004.md)).

## 7. 현재 상태 (2026-11-24, 우리 프레임버퍼가 화면을 맡음)

- 본 kext 0.0.23(UUID `0E699920-…`) 보조 collection, 보조 kext VirtioVideoNdrvBlocker 0.0.1(UUID `DF995B63-…`) OpenCore 주입
- IONDRV 없음, `VirtioVideoFramebuffer` 1280×800, scanout 동작 중. boot-args `keepsyms=1`
- 백업: `config.plist.bak-20261124-ndrvblocker`(보조 kext 추가 전), 그 앞의 `…-dptest`, `…-wi2005-ignorendrv`, `…-wi2005-untakeover`, `…-wi2005-takeover`

### 이전 상태 (보조 kernel collection 전환 후)

- 로드 경로: **`/Library/Extensions/VirtioVideo.kext` 0.0.21(UUID `95883E93-…`), 보조 kernel collection**. OC `Kernel > Add:8` VirtioVideo는 `Enabled=false`(EFI의 kext 파일은 남아 있음)
- SIP: `csr-active-config` `01020000` (Kext Signing만 해제), `NVRAM > Delete`에 `csr-active-config`
- 확인: driver-ok(0x0F), `0:1280x800`, 프레임버퍼 물리 연속, `VirtioVideoFramebuffer` 클래스 등록(인스턴스 0), 화면은 IONDRV

### 이전 상태 (스냅샷 복구 후 WI-2.004 재설치)

- boot-args: `keepsyms=1`, OpenCore `Misc > Debug > Target` 0 (파일 로그 꺼짐)
- `Kernel > Add:8` VirtioVideo: `Enabled=true` (재설치 때 다시 추가)
- KDK `KDK_15.8.3_24H241.kdk` 재설치함 (커널 UUID `747934E9-…` 일치)
- EFI의 kext 버전: 0.0.19 (커밋 `0c69321` 코드를 다시 빌드), UUID `F4919C2E-B38E-3497-828D-BBD33E63CB5A` (dSYM은 `build/dsym-archive/`). 원래 WI-2.004 빌드 UUID `6EFE566A-…`의 dSYM은 복구로 사라졌다.
- 재설치 후 확인 (2026-11-24): `VVInitResult=driver-ok`, 상태 15(0x0F), `VVDisplayInfo=0:1280x800`, `VVFramebuffer=1280x800 bytes=4096000 contiguous=1 dma-identity=1`, `VVContigProbe=2560x1600 ok-contiguous`, `VVSelfTest … pass`, MSI-X, 명령 2·시간 초과 0·복구 0, 새 패닉 없음. 화면 1280×800(IONDRV). 시험 패턴은 다시 돌리지 않았다.
- 기능 요약: 시작 때 선호 모드 프레임버퍼(물리 연속)를 할당해 둔다. 2D resource 명령을 갖췄고, boot-arg `vvideo_test_pattern=1`이면 시작 90초 뒤 색 띠를 15초 보여 준 뒤 스스로 되돌린다. `VirtioGpuDevice`가 GPU 장치를 DRIVER_OK(0x0F)까지 초기화하고 `GET_DISPLAY_INFO`를 주고받으며, 장치가 reset되면 스스로 복구한다. 디버그 빌드는 시작 때 오류 응답 자체 시험(`VVSelfTest`)을 한다. 화면은 IONDRV가 그린다
- boot-args `keepsyms=1` (기본값으로 되돌림, 2026-11-24)
- 디버그 boot-arg: `vvideo_log=0|1|2`(로그 수준), `vvideo_irq=0|1|2`(MSI-X/INTx/Poll), `vvideo_test_reset=1`(30초 뒤 외부 reset 흉내), `vvideo_test_pattern=1`(90초 뒤 색 띠 시험), `-vvideooff`(드라이버 끄기)
- boot-arg `vvideo_irq=0|1|2`로 인터럽트 방식을 고를 수 있다 (이 장치에서 1은 Poll로 내려간다)
- QEMU: `-gdb tcp:127.0.0.1:1234` 사용 중
