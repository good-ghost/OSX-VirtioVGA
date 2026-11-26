# 디버깅 절차

[English](DEBUGGING.en.md) | 한국어

대상: OSX-KVM 게스트 macOS 15.8.3 (빌드 `24H241`, x86_64), 보조 kernel collection으로 로드한 `VirtioVGA.kext`와 OpenCore로 주입한 `VirtioVGANdrvBlocker.kext`. 2026-11-25 기준(WI-5.009).
설치·끄기·롤백은 [INSTALL.md](INSTALL.md)를 봐 주세요.

| 상황 | 쓸 방법 | 상태 |
| ---- | ------- | ---- |
| 드라이버 동작 확인 | §1 `scripts/vvstat.sh`, 드라이버 로그(`VVLog`) | 확인됨 (WI-0.005, WI-5.008) |
| 커널 패닉 | §2 패닉 보고서 + `scripts/panic-summary.py` | 확인됨 (합성 보고서로) |
| 멈추거나 상태를 직접 봐야 할 때 | §3 QEMU gdbstub + lldb (호스트에서) | kext 심볼·메모리 읽기 확인. 커널 심볼 불가, 중단점 미확인 |
| Apple 코드 동작 확인 (IONDRV, IOFramebuffer) | §3.5 KDK 심볼로 정적 조회 (게스트에서) | 확인됨 |

## 1. 드라이버 상태와 로그

### 1.1 상태 조회 도구

```bash
scripts/vvstat.sh --log 20
```

IORegistry 속성만 읽습니다(root 불필요). 종료 코드 0이면 `driver-ok`이고 프레임버퍼가 붙어 있습니다. `--all`은 `VV*` 속성 전체를 보입니다.

| 줄 | 속성 | 정상 예 | 볼 것 |
| -- | ---- | ------- | ----- |
| `init` | `VVInitResult` | `driver-ok` | 다른 값이면 초기화 실패 단계 이름(예: `transport`, `interrupts`, `recovery-limit`) |
| `mode` | `VVCurrentMode` | `2560x1600 changes=1` | 해상도와 전환 횟수 |
| `scanout` | `VVScanout` | `running … flush-errors=0 blanks=0` | `flush-errors`가 늘면 전송 실패(resource를 다시 만듭니다). `blanks`는 디스플레이 잠자기 횟수 |
| `display power` | `VVDisplayPower` | `on sleeps=N wakes=N` | 잠자기·깨우기 횟수가 같아야 합니다. 한 번도 안 잤으면 `-` |
| `cursor` | `VVCursor` | `… drops=0 rejects=0 scale=50` | `drops`(인터럽트 문맥 등으로 버린 이동), `rejects`(64×64 초과 이미지) |
| `dirty flush` | `VVDirtyStats` | `on=0 …` | 변경 감지 전송 통계 (기본 꺼짐) |
| `auto resize` | `VVAutoResizeState` | `on handler=enabled connect-changes=N last=WxH` | 창 크기 자동 전환 (INSTALL §5.4). `handler=none`이면 IOGraphics가 연결 변경 콜백을 등록하지 않은 것 |
| `edid` | `VVEdid` | `ok size=1024` | `invalid:…`이면 장치가 준 EDID를 거절 |
| `events` | `VVEventCounters` | `… device-errors=0 … timeouts=0 errors=1 recoveries=0` | `errors=1`은 부팅 때 자체 시험이 일부러 만든 오류입니다. `timeouts`·`device-errors`·`recoveries`가 늘면 장치 문제 |

### 1.2 드라이버 로그

보조 kernel collection·OpenCore로 로드한 kext의 `IOLog`/`os_log`는 통합 로그와 `dmesg`에 남지 않습니다 (WI-0.005 실측). 드라이버는 로그를 내부 링 버퍼(128줄)에 쌓고 IORegistry 속성 `VVLog`로 공개합니다. `vvstat --log N`으로 보거나 직접 읽습니다.

```bash
ioreg -r -c VirtioGpuDevice -l -w0 | grep '"VV'
```

- 클래스: `VirtioGpuDevice`(장치·전송·커서), `VirtioVideoFramebuffer`(`VVFbTrace` IOGraphics 호출 순서, `VVFbPowerTrace` 전원·연결 속성 최근 24개), `VirtioVideoProbe`. `VirtioGpuDevice`는 `registerService`를 하지 않으므로 `IOServiceGetMatchingService`로는 찾을 수 없습니다(`vvstat`은 레지스트리를 돌며 찾습니다).
- 수준: boot-args `vvideo_log=0|1|2` (0 오류, 1 정보 — 기본, 2 디버그). [INSTALL.md §5.1](INSTALL.md)
- 재부팅하면 로그가 사라집니다. 시스템이 멈춰 강제로 끄면 그 직전 상태를 볼 수 없으므로(WI-4.003, WI-5.003), 재현할 때는 다른 기기에서 Remote Control로 미리 `vvstat`을 읽거나 `scripts/soak-snapshot.sh`로 기록해 두세요.
- 시스템 로그를 볼 때는 `/usr/bin/log show ...`를 써 주세요. zsh에서 `log`는 내장 명령이라 조용히 실패합니다.

## 2. 커널 패닉

### 2.1 보고서 위치

`/Library/Logs/DiagnosticReports/Kernel-<날짜>-<시각>.panic` — 일반 사용자로 읽을 수 있습니다 (sudo 불필요).
형식(macOS 15 실측): 1행 메타데이터 JSON, 2행부터 본문 JSON. 패닉 문자열은 본문의 `macOSPanicString`.

### 2.2 요약과 소스 줄 변환

```bash
python3 -I scripts/panic-summary.py
```

인자를 주지 않으면 가장 최근 보고서를 읽습니다. 특정 파일은 경로를 인자로 주세요.

출력:

- 시각, OS 버전, 패닉 문자열 앞 3줄
- 이 kext(`local.virtiovga.VirtioVGA`)가 "Kernel Extensions in backtrace"에 있으면 버전·UUID·로드 범위
- 그 범위에 들어가는 백트레이스 주소를 `atos`로 바꾼 함수·파일·줄

### 2.3 dSYM과 UUID

- `scripts/build.sh`가 `build/VirtioVGA.kext.dSYM`을 만들고, `build/dsym-archive/<UUID>.dSYM`에도 보관합니다.
- **빌드할 때마다 UUID가 바뀝니다** (디버그 맵에 오브젝트 파일 시각이 들어가기 때문으로 보입니다). 그래서 패닉 보고서의 UUID와 같은 dSYM이 필요하고, `panic-summary.py`는 현재 dSYM이 맞지 않으면 보관소에서 UUID로 찾습니다.
- 설치한 빌드의 dSYM이 보관소에 있어야 합니다. `build/`를 지우면 보관소도 사라집니다.
- 손으로 바꿀 때:

  ```bash
  atos -o build/dsym-archive/<UUID>.dSYM/Contents/Resources/DWARF/VirtioVGA -arch x86_64 -l <kext 시작 주소> <주소> ...
  ```

### 2.4 개발 중 난 패닉 (이 프로젝트)

| 보고서 | 원인 | 처리 |
| ------ | ---- | ---- |
| `Kernel-2026-11-24-052540.panic` | 실행 중 IONDRV `terminate()` → `AppleUpstreamUserClientDriver::fbNotificationOccurred` NULL 참조 | 실행 중 넘겨받기를 버리고 부팅 때 보조 kext로 IONDRV를 막는 방식으로 바꿈. `vvideo_takeover=1`은 시험용으로만 남김 |
| `Kernel-2026-11-24-054128.panic` | 같은 넘겨받기 시험 | 같음 |
| (보고서 없음) 2026-11-25 01:49 | 화면 사용 중 `kmutil unload` → 패닉 상태로 멈춤, 강제 종료 | NFR-003을 재부팅 교체로 변경(WI-5.003) |

### 2.5 이 VM에 이미 있던 패닉 (이 프로젝트와 무관)

2026-11-19, 11-22, 11-23 18:53에 `Kernel-*.panic`이 있습니다. 모두 드라이버 설치 전이고 당시 OS는 macOS 15.7.9 (24G830)입니다. 패닉 문자열은 `mp_kdp_enter() timed-out` → `NMIPI for spinlock acquisition timeout`, 백트레이스는 `xcpm_*`(CPU 전원 관리)입니다. 원인은 조사하지 않았습니다. 드라이버 시험 중 같은 패닉이 나면 이 드라이버 때문이 아닐 수 있으므로 위 형태와 비교해 주세요.

## 3. QEMU gdbstub + lldb (호스트에서 실행)

게스트 커널은 게스트 안에서 디버깅할 수 없습니다 (디버거 자체가 멈춥니다). **호스트(Linux)에서 lldb로 QEMU gdbstub에 붙습니다.** 이 절의 절차는 아직 실행해 보지 않았습니다.

### 3.1 KDK 설치 (게스트, 사용자 작업) — 2026-11-23 완료

설치 결과: `/Library/Developer/KDKs/KDK_15.8.3_24H241.kdk` (Apple Developer `Kernel_Debug_Kit_15.8.3_build_24H241.dmg`, 925,610,504바이트, Apple 서명 패키지). 실행 중인 커널과 KDK 커널의 UUID가 `747934E9-FD01-36AB-8E97-06A578AB22F9`로 같습니다. KDK에는 `kernel.dSYM`과 `IOGraphicsFamily`·`IONDRVSupport`·`IOPCIFamily`의 `.kext.dSYM`이 있습니다.

절차 (OS를 업데이트하면 새 빌드용으로 다시 설치해 주세요):

1. https://developer.apple.com/download/all/ 에 Apple ID로 로그인해 "Kernel Debug Kit"을 검색해 주세요.
2. **macOS 15.8.3, 빌드 24H241**용을 받아 주세요. 같은 빌드가 없으면 가장 가까운 15.x 빌드를 받고 그 사실을 기록해 주세요 (R-07). 빌드가 다르면 커널 심볼이 맞지 않을 수 있습니다.
3. `.pkg`를 설치해 주세요 (관리자 비밀번호 필요). 설치 위치: `/Library/Developer/KDKs/KDK_<버전>_<빌드>.kdk`
4. 확인:

   ```bash
   ls /Library/Developer/KDKs
   ```

   ```bash
   dwarfdump --uuid /System/Library/Kernels/kernel
   ```

   ```bash
   dwarfdump --uuid /Library/Developer/KDKs/<KDK 이름>.kdk/System/Library/Kernels/kernel
   ```

   두 UUID가 같아야 KDK 커널 심볼을 쓸 수 있습니다.

### 3.2 QEMU 옵션 (호스트, 사용자 작업) — 2026-11-23 적용

OSX-KVM 실행 스크립트의 QEMU 인자에 `-gdb tcp:127.0.0.1:1234`를 넣었습니다. `-s`(= `-gdb tcp::1234`)는 모든 인터페이스에서 인증 없이 받으므로 쓰지 않습니다. 부팅 초기부터 멈춰야 할 때만 `-S`를 함께 씁니다 (gdb가 `continue`할 때까지 CPU 정지).

### 3.3 호스트로 심볼 복사

호스트의 lldb는 distrobox 컨테이너 `gen-dev`에 있습니다 (LLVM lldb 19.1.7, `plugin.dynamic-loader.darwin-kernel` 있음). 컨테이너는 호스트 네트워크를 공유하므로 `127.0.0.1:1234`로 gdbstub에 닿습니다.

1. 게스트: 시스템 설정 > 일반 > 공유 > 원격 로그인을 켜 주세요 (사용자 작업). 게스트 IP: `ipconfig getifaddr en0` (2026-11-23: `192.168.122.75`).
2. 게스트: 심볼 묶음을 만들어 주세요. 파일 이름에 kext UUID가 들어갑니다.

   ```bash
   U=$(dwarfdump --uuid build/VirtioVGA.kext/Contents/MacOS/VirtioVGA | awk '{print $2}'); K=/Library/Developer/KDKs/KDK_15.8.3_24H241.kdk/System/Library/Kernels; D=build/debug-symbols; rm -rf $D && mkdir -p $D/vv-debug && cp -R $K/kernel $K/kernel.dSYM build/VirtioVGA.kext/Contents/MacOS/VirtioVGA $D/vv-debug/ && cp -R build/dsym-archive/$U.dSYM $D/vv-debug/VirtioVGA.dSYM && xattr -cr $D/vv-debug && COPYFILE_DISABLE=1 tar -C $D -czf $D/vv-debug-$U.tar.gz vv-debug
   ```

3. 호스트(`distrobox enter gen-dev`): `scp`로 가져와 풀어 주세요.

   ```bash
   mkdir -p ~/vv-debug-host && cd ~/vv-debug-host && scp woomg@<게스트 IP>:Projects/VirtioVGA/build/debug-symbols/vv-debug-<UUID>.tar.gz . && tar -xzf vv-debug-<UUID>.tar.gz
   ```

### 3.4 lldb 연결 (호스트) — 2026-11-23 확인

**경고: 붙은 상태에서 `quit`/Ctrl-D로 lldb를 끝내지 마세요.** lldb가 kill을 보내면 QEMU가 종료되어 VM이 꺼질 수 있습니다. 항상 `process detach` 후 `quit`해 주세요. 같은 이유로 `--batch`로 붙지 마세요. 붙어 있는 동안 VM 전체가 멈춥니다.

**커널 심볼은 쓸 수 없습니다.** lldb 19.1.7(Linux)은 KDK 커널 바이너리를 여는 순간 segmentation fault로 죽습니다. dSYM을 치운 상태, `--no-dependents`, `target.load-script-from-symbol-file false`에서도 같았습니다. 스택 덤프에 같은 프레임 4~5개가 반복되어 무한 재귀로 보입니다 (추정). 그래서 커널 자동 탐색을 끄고 kext만 붙입니다. 백트레이스의 커널 쪽은 주소로만 보입니다. 커널 주소는 게스트의 §3.5(KDK 정적 조회)로 찾을 수 있지만 KASLR 슬라이드를 따로 알아야 합니다.

게스트에서 kext 로드 주소를 확인해 주세요 (`Address` 열).

```bash
kmutil showloaded | grep -i virtiovga
```

호스트 lldb (한 줄씩):

```
settings set plugin.dynamic-loader.darwin-kernel.scan-type none
settings set plugin.dynamic-loader.darwin-kernel.load-kexts false
target create --no-dependents vv-debug/VirtioVGA
target symbols add vv-debug/VirtioVGA.dSYM/Contents/Resources/DWARF/VirtioVGA
gdb-remote 127.0.0.1:1234
target modules load --file VirtioVGA --slide <kmutil의 Address 값>
memory read -f s -c 1 <Address + kmod_info 오프셋 + 0x10>
process detach
quit
```

- kext `__TEXT`의 vmaddr가 0이라 로드 주소가 그대로 슬라이드입니다.
- `kmod_info` 오프셋: `nm build/VirtioVGA.kext/Contents/MacOS/VirtioVGA | grep ' _kmod_info$'`. 구조체의 `name` 필드가 +0x10에 있어 bundle id 문자열이 읽혀야 합니다.
- `target symbols add`에 `.dSYM` 번들 경로를 주면 "does not match any existing module"로 실패했습니다. 번들 안 DWARF 파일 경로를 주는 형태로 적었습니다 (이 형태는 아직 실행해 보지 않았습니다).

2026-11-23 결과 (kext 0.0.5, UUID `935ADD8D-1FAD-37A7-A84E-72A47E4D92F7`, 로드 주소 `0xffffff80040e4000`):

| 단계 | 결과 |
| ---- | ---- |
| `gdb-remote 127.0.0.1:1234` | 연결됨, `stop reason = signal SIGTRAP`. 첫 프레임 주소는 `0xfff0`로 표시됐지만 `register read rip`은 `0xffffff801d739d29`(커널 주소) |
| `target modules load --slide` | 성공 |
| `memory read -f s 0xffffff80040e69a0` (`kmod_info`+0x10) | `"local.virtiovga.VirtioVGA"` — 실행 중인 메모리와 kext 심볼 주소가 일치 |
| `process detach` | VM 재개 |

확인하지 못한 것

- **중단점에서 멈추기**: 지금 kext 코드는 부팅 때 `start()`만 실행하고 실행 중에는 호출되는 경로가 없어 시험하지 못했습니다. P1 이후 인터럽트·타이머 경로가 생기면 `breakpoint set --name <함수>`로 확인합니다.
- **부팅 중 `start()`에서 멈추기**: kext 로드 전에는 주소를 알 수 없습니다. `-S`로 시작해 kext 로드 직후 멈추는 방법은 아직 찾지 못했습니다. 필요해지면 그때 조사합니다. 그 전까지는 §1 로그와 §2 패닉 분석을 씁니다.
- **커널 심볼**: 더 새 lldb(LLVM 20 이상)에서 커널 바이너리를 열 수 있는지는 시험하지 않았습니다 (컨테이너에 새 패키지 설치가 필요합니다).

### 3.5 KDK 심볼로 정적 조회 (게스트에서, 호스트 불필요)

실행 중인 커널에 붙지 않고도 Apple 코드의 함수 위치와 소스 줄을 볼 수 있습니다. P2에서 `IONDRVFramebuffer`가 어떻게 물러나는지 조사할 때 씁니다 (WI-2.006).

```bash
lldb --batch -o "target create /Library/Developer/KDKs/KDK_15.8.3_24H241.kdk/System/Library/Kernels/kernel" -o "image lookup -n PE_parse_boot_argn"
```

```bash
lldb --batch -o "target create /Library/Developer/KDKs/KDK_15.8.3_24H241.kdk/System/Library/Extensions/IOGraphicsFamily.kext/IOGraphicsFamily" -o "image lookup -r -n 'IONDRVFramebuffer::'"
```

확인: 첫 명령이 `PE_parse_boot_argn at bootargs.c:196`을 돌려줍니다.

