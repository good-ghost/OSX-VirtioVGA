# WI-001 스냅샷 이후 작업 기록 (WI-0.002 ~ WI-2.005)

| 항목      | 내용 |
| --------- | ---- |
| 작성일    | 2026-11-24 |
| 출처      | Claude Code Remote Control 세션 기록 ("Virtio-Video 드라이버 프로젝트 구성", 공유 링크 `cshk_01Smw…`). 내장 브라우저에서 전체 메시지 332개를 모아 읽었다 |
| 범위      | 메시지 [35]~[302]: WI-0.001 완료 직후("Not yet")부터 WI-2.005 실패와 스냅샷 복구까지 |
| 왜 만들었나 | 사용자가 VM을 호스트 스냅샷 "WI-001"(WI-0.001 완료 시점)으로 복구하면서 VM 안의 작업 폴더·대화 기록·KDK·EFI 변경이 모두 되돌아갔다. WI-2.004까지는 GitHub(`origin/p0/dev-environment`, 마지막 커밋 `0c69321`)에 남았지만 **WI-2.005는 커밋 전이라 코드가 유실됐다.** 이 문서는 그 사이의 사실을 남기기 위한 것이다 |
| 제외한 것 | 대화에 있던 비밀번호, SSH 키 지문, 개인 연락처. 세션 화면에서 접혀 있던 명령 출력 원문은 볼 수 없어 요약만 남겼다 |

## 1. 요약

- WI-0.002부터 WI-2.004까지 18개 WI(P0 7개, P1 7개, P2 4개)를 진행했고, 모두 커밋·푸시됐다 (커밋 목록은 §2).
- 첫 화면 출력(WI-2.004)까지 성공했다. 색 띠가 바르게 나왔고, 장치 reset으로 원래 VGA 화면에 돌아오는 것도 확인했다.
- WI-2.005(IOFramebuffer 하위 클래스, kext 0.0.20)는 코드를 다 쓰고 EFI에 설치했지만, **첫 재부팅에서 kext가 아예 로드되지 않았다.** 원인 가설은 T-04(아래 §4.4).
- 원인을 보려고 **OpenCore 파일 로그(Misc > Debug > Target=65)를 켜고 재부팅하자 Apple 로고에서 멈췄다.** 사용자가 스냅샷으로 복구했다.
- 따라서 "WI-2.005 코드 때문에 부팅이 멈췄다"는 것은 확인된 사실이 아니다. 멈춘 부팅 직전의 마지막 변경은 OpenCore 로그 설정이었다 (§5).

## 2. 진행 기록 (커밋·kext 버전)

| WI | kext | 커밋 | 결과 요약 |
| -- | ---- | ---- | --------- |
| WI-0.002 | 0.0.1 | `e8098aa` | OC `Kernel > Add` 맨 끝에 항목 추가(MinKernel 24.0.0)로 로드 성공. 부팅 초기 "IOGraphicsFamily not found"가 15번 기록된 뒤 로드됨 → **T-04** 발견 |
| WI-0.005 | 0.0.2~0.0.3 | `e8098aa` | IOLog·os_log 메시지가 통합 로그·dmesg 어디에도 없음(OC 주입 kext). 드라이버 안 링 버퍼(128줄)를 IORegistry `VVLog`로 공개하는 방식으로 결정. boot-arg `vvideo_log=0|1|2` |
| WI-0.003 | 0.0.4 | `e8098aa` | `-vvideooff`(kext 로드, 객체 없음)와 `Enabled=false`(kext 미로드) 둘 다 실기 확인. INSTALL.md 작성 |
| WI-0.006, 0.008 | — | `9e11de5` | 단위 테스트 하네스(`scripts/test.sh`, ASan·UBSan), virtio 사양 헤더. 픽셀 형식 B8G8R8X8_UNORM=2로 계획 정정 |
| WI-0.007 | — | `ab0a6ef` | `collect-env.sh --save`, `compare-env.sh` |
| WI-0.004 | 0.0.5 | `69b83c8` | KDK 15.8.3(24H241) 설치(UUID `747934E9-…` 일치), dSYM UUID별 보관, `panic-summary.py`. 호스트 lldb 19.1.7은 KDK 커널을 열면 segfault → kext만 붙여 gdbstub 연결 성공 |
| WI-1.001 | 0.0.6~0.0.7 | `846f9ad` | Apple `AppleVirtIOPCITransport`가 먼저 장치를 차지(ACK=0x01). 점수 20000으로도 밀어내지 못함(로드 시점 문제) → **T-05**, 다른 카테고리로 공존 결정 |
| WI-1.002 | 0.0.9 | `38fa412` | 영역: common 2:0x1000, isr 2:0x1800, device 2:0x2000, notify 2:0x3000(mult 4) |
| WI-1.003 | 0.0.10 | `07855ff` | 장치 제공 feature: EDID, INDIRECT_DESC, EVENT_IDX, VERSION_1 (RING_RESET 없음). 협상: VERSION_1+EDID. 상태 0x0B |
| WI-1.004 | 0.0.11 | `0a48d66` | controlq 64 / cursorq 16(장치 최대 1024), DRIVER_OK(0x0F). DRIVER_OK만으로는 VGA 출력 유지. `com.apple.kpi.mach` 누락을 빌드 검사로 발견 |
| WI-1.005 | 0.0.12~0.0.14 | `050c11a` | `configureInterrupts`는 kIOReturnUnsupported(PCI 계층이 MSI-X 소스 1개를 미리 구성). 기존 소스 공유 벡터 0 사용. INTx 소스 없음. GTK 창 크기 변경으로 config 이벤트 안 옴 |
| WI-1.007 | 0.0.15 | `38916dd` | GET_DISPLAY_INFO 왕복 `0:1280x800`. MSI-X(interrupts=1)와 Poll(10ms) 모두 성공 |
| WI-1.006 | 0.0.16 | `eb4d7a8` | 외부 reset 흉내(`vvideo_test_reset=1`) → 감지·재초기화·명령 재전송 성공(recoveries=1). `thread_call_cancel_wait`(kpi.private) 사용을 빌드 검사가 잡아 공개 KPI로 교체 |
| WI-2.001 | 0.0.17 | `bb05a83` | `sendCommand`/`sendNodataCommand`, 명령 이름 로그, 디버그 자체 시험(`ERR_INVALID_RESOURCE_ID`) |
| WI-2.002, 2.003 | 0.0.18 | `56b98f3` | 선호 모드 1280×800. **T-01 해결**: 프레임버퍼 물리 연속 할당, 2560×1600(16,384,000B)도 부팅 직후 연속 할당 성공, IOMMU 없음 |
| WI-2.004 | 0.0.19 | `0c69321` | 2D resource 명령 7종, 시험 패턴(`vvideo_test_pattern=1`). 빨강·초록·파랑·흰색 정상(AC-04). scanout 끄면 "Display output is not active", **장치 reset하면 VGA로 복귀** |
| WI-2.005 | 0.0.20 | **없음 (유실)** | §4 |

세부 실측값은 각 PHASE-PLAN의 "결정 기록"과 INSTALL.md에 커밋돼 있다.

## 3. 환경·운영 사실 (커밋 문서에 일부만 있음)

- **EFI 마운트**: 매번 사용자가 터미널 탭에서 `sudo diskutil mount disk0s1`에 비밀번호를 입력했다. EFI 작업(백업 → kext 복사 → config 수정 → `plutil -lint`)은 Claude가 했다.
- **EFI config 백업**: `EFI/OC/config.plist.bak-<날짜>-<WI>` 형식. WI-2.005 때 `config.plist.bak-20261124-wi2005`(kext 0.0.20 설치 전), `config.plist.bak-20261124-wi2005-oclog`(OC 로그 켜기 전)가 추가됐다. 스냅샷 복구로 EFI도 되돌아갔으므로 이 파일들은 지금 EFI에 없을 것으로 본다 (확인하지 않음).
- **OpenCore**: REL-106-2025-11-03 (RELEASE 빌드). Recovery는 `HideAuxiliary=true`라 부팅 선택 화면에서 Space로 보이게 한다. boot-args는 OC가 매 부팅 config 값으로 다시 쓰므로 `nvram`이 아니라 config.plist를 고쳐야 한다.
- **기존 config의 불일치**: `Kernel > Add`에 있는 5개(VoodooPS2 2개, AppleMCEReporterDisabler, USBToolBox, UTBMap)가 `Kexts/`에 없고, 반대로 `Kexts/`의 USBPorts·AppleALC 등은 등록돼 있지 않다. 손대지 않았다.
- **로그**: zsh에서 `log`는 내장 명령이라 `/usr/bin/log`를 써야 한다. OC 주입 kext의 IOLog/os_log는 통합 로그에 남지 않는다.
- **기존 패닉**: 드라이버 설치 전부터 xcpm spinlock 시간 초과 패닉 3건(11-19, 11-22, 11-23 18:53, macOS 15.7.9 시절). 이 프로젝트와 무관 (DEBUGGING.md §2.4).
- **원격 접속**: 게스트 원격 로그인(SSH) 켜짐, 게스트 IP 192.168.122.75. 화면이 검게 남을 때 호스트에서 SSH로 boot-args를 되돌리는 절차가 INSTALL.md §5.3.
- **호스트 디버깅**: 호스트 QEMU에 `-gdb tcp:127.0.0.1:1234`, 호스트 distrobox 컨테이너(gen-dev)의 lldb 19.1.7 사용. 연결 중 `quit`하면 QEMU가 꺼질 수 있어 반드시 `process detach` 먼저.
- **git**: 저장소 로컬 설정으로 작성자 good-ghost, 원격 `git@github.com:good-ghost/OSX-VirtioVGA.git`, 브랜치 `p0/dev-environment`(main 없음). 푸시는 SSH 키 패스프레이즈를 사용자가 터미널 탭에 입력했다.
- **Sonoma 캡처**: `docs/screenshots/before/2026-11-23-desktop-sonoma-1280x800.png`이 Finder에서 지워진 상태로 계속 커밋에서 제외됐다.

## 4. WI-2.005 상세 (유실된 작업)

### 4.1 사전 조사

- KDK 심볼과 IOGraphics 소스에서, PCI 장치에 `AAPL,iokit-ignore-ndrv` 속성이 있으면 `IONDRVFramebuffer` 매칭을 막는다는 단서를 찾았다.
- 실측: `IONDRVFramebuffer`(`.Display_boot`)는 GPU와 **같은 PCI 장치(S00@0)** 에 `IOMatchCategory IOFramebuffer`, `IOProbeScore 20000`으로 붙어 있다.
- 우리 kext는 부팅 후반(IOGraphicsFamily 로드 뒤)에 로드되므로(T-04), 이미 붙은 IONDRV를 넘겨받는 방식이 필요하다고 판단했다.

### 4.2 구현 (kext 0.0.20, 빌드 종료 코드 0, 단위 테스트 63개 통과)

| 파일 | 내용 |
| ---- | ---- |
| `VirtioVideoFramebuffer.hpp` (+59) / `.cpp` (+286) | `IOFramebuffer` 하위 클래스. 모드는 장치 선호 해상도 1개(1280×800), 32bpp. aperture는 물리 연속 프레임버퍼. 소프트웨어 커서, 감마 테이블은 받기만 함. **프레임버퍼 객체가 `VVDmaBuffer`를 직접 retain** (GPU 서비스가 먼저 멈춰도 aperture가 해제된 메모리를 가리키지 않게) |
| `VirtioGpuDeviceScanout.cpp` | 프레임버퍼로 resource를 만들어 scanout 0에 연결, 약 16ms마다 전체 transfer + flush. 전송 실패 시 1초 간격으로 resource 재생성 |
| (타이밍 도우미, 단위 테스트 포함) | takeover 지연·주기 계산. 테스트 수 63개 |
| Info.plist personality | 프레임버퍼: `IOMatchCategory IOFramebuffer`, **`IOProbeScore 30000`**. boot-arg가 없으면 매칭 단계에서 빠짐 |

takeover 순서 (WI-2.006 범위를 함께 넣음, boot-arg `vvideo_takeover=1`일 때만):

1. 드라이버 시작 **60초 뒤** 시작 (패닉이 매 부팅·Recovery에서 반복될 때 boot-args를 고칠 시간을 주려고)
2. PCI 장치에 `AAPL,iokit-ignore-ndrv`를 붙여 IONDRV가 다시 붙지 못하게 함
3. `IONDRVFramebuffer`를 종료하고 장치에서 떨어질 때까지 최대 10초 대기
4. 장치 매칭을 다시 돌려 우리 프레임버퍼가 붙게 함
5. 상태는 IORegistry `VVTakeover`, `VVScanout`으로 확인

### 4.3 설치 (메시지 [290]~[291])

- kext 0.0.20(UUID `E62BBC70-…`)을 EFI에 복사, 빌드본과 같은 파일임을 확인
- boot-args: `keepsyms=1` → **`keepsyms=1 vvideo_takeover=1`**
- 백업: `config.plist.bak-20261124-wi2005`
- 호스트 스냅샷은 VM을 꺼야 만들 수 있어 Claude가 만들지 못했다고 안내함

### 4.4 첫 재부팅 결과: kext 미로드 (메시지 [293]~[296])

- 사용자 관찰: 화면 변화 없음
- `kmutil showloaded`에 kext 없음, `VirtioGpuDevice`·`VirtioVideoProbe` 객체도 없음
- 부팅 로그에 kext 이름이 한 줄도 없음. WI-0.002 때 보였던 "IOGraphicsFamily not found"도 없음
- 당시 가설 (확인 못 함): OpenCore가 주입 단계에서 kext를 버렸다.
  - OC는 kext를 부트 kernel collection에 넣으면서 직접 링크한다.
  - 0.0.20부터 `IOFramebuffer`를 상속하므로 링크에 IOGraphicsFamily의 심볼·vtable이 필요하다.
  - IOGraphicsFamily는 시스템 kernel collection에만 있고 디스크에는 바이너리가 없다(`IOGraphicsFamily.kext`에 `Contents/MacOS` 없음).
  - 0.0.19까지는 IOGraphicsFamily 심볼을 쓰지 않아 드러나지 않았다 (T-04).
- 당시 제시한 대안 (둘 다 결정 변경·위험 있음): `/Library/Extensions` 설치(Q-01 변경 필요), KDK의 IOGraphicsFamily 함께 주입(중복 kext 위험). 로그로 원인 확정 후 추천하기로 함

### 4.5 OpenCore 로그 켜기와 부팅 멈춤 (메시지 [297]~[300])

- EFI config 변경: `Misc > Debug > Target` 0 → **65**(로그 + 파일 저장), `DisplayLevel`에 INFO 수준 추가. `plutil -lint` OK
- 백업: `config.plist.bak-20261124-wi2005-oclog`
- EFI의 kext UUID가 빌드본과 같음(`E62BBC70-…`)을 확인 — 복사 오류는 아님
- 안내: RELEASE 빌드에서 파일 로그를 켜면 부팅이 느려질 수 있음
- **사용자 보고: 재부팅 시 Apple 로고 뒤로 진행되지 않음 → WI-001 스냅샷으로 복구**

## 5. 원인 판단 (2026-11-24 재검토)

확인된 사실만으로 보면 다음과 같다.

1. kext 0.0.20은 `vvideo_takeover=1` 상태로 한 번 부팅했지만 **로드되지 않았고, 부팅은 정상이었다.**
2. 그다음 바뀐 것은 **OpenCore 로그 설정(Target=65, DisplayLevel INFO)뿐**이고, 그 부팅에서 Apple 로고 뒤로 진행되지 않았다.
3. 패닉 보고서, OC 로그 파일, 화면 사진은 스냅샷 복구로 모두 사라졌다.

따라서 멈춤의 직접 원인 후보는 다음 순서로 본다 (모두 확인 못 함).

| 순위 | 후보 | 근거 |
| ---- | ---- | ---- |
| 1 | OC 파일 로그(Target=65)·DisplayLevel 변경으로 인한 극심한 지연 또는 멈춤 | 직전 변경이 이것뿐. RELEASE 빌드 파일 로그는 FAT 볼륨에 계속 쓰며 부팅을 크게 늦출 수 있다(일반적인 OC 동작으로 알려져 있으나 이 VM에서 측정하지 않음) |
| 2 | OC 로그 설정과 무관하게, 같은 kext·boot-args 상태에서 두 번째 부팅만 다르게 진행 | 첫 부팅은 kext 미로드로 정상이었음. 재현 근거 없음 |
| 3 | kext 0.0.20이 실제로 로드되어 멈춤 | 첫 부팅에서 로드 자체가 안 됐으므로 가능성 낮음 |

이전 PHASE-PLAN-02 기록의 "WI-2.005 kext로 재부팅하면 Apple 로고에서 멈췄다"는 표현은 이 기록과 맞지 않아 수정이 필요하다.

## 6. 현재 상태 (2026-11-24 복구 후)

- VM: WI-001 스냅샷 시점. 작업 폴더는 GitHub `0c69321`(WI-2.004)로 맞춤. 이전 로컬 파일은 스크래치 패드의 `local-pre-sync-20261124.tgz`에 백업
- EFI: 스냅샷 시점으로 되돌아간 것으로 본다. `kmutil showloaded`에 VirtioVideo 없음, boot-args `keepsyms=1` (확인함). OC 로그 설정과 백업 파일 존재 여부는 EFI를 마운트하지 않아 확인하지 않음
- KDK: 지워짐(`/Library/Developer/KDKs` 없음). 재설치 필요 (DEBUGGING.md §3.1)
- WI-2.005 코드: 유실. 이 문서 §4.2가 남은 설계 기록이다

## 7. 재시도할 때 적용할 것

- **T-04부터 확인한다.** IOGraphicsFamily 심볼을 쓰는 kext가 OC 주입으로 로드되는지, 최소 IOFramebuffer 하위 클래스(메서드 스텁만)로 먼저 시험한다. 이때 OC 파일 로그는 켜지 않고, 로그가 필요하면 화면 로그(`-v`)나 호스트 직렬 로그(`-serial file:…`, boot-arg `serial=3`)를 먼저 쓴다.
- OC 로그 설정을 다시 켜야 하면 kext 변경과 **다른 부팅**에서 단독으로 바꿔, 원인이 섞이지 않게 한다.
- 매 시험 전에 VM을 끄고 호스트 스냅샷을 만든다.
- **WI가 끝날 때마다, 그리고 실기 설치 전에도 커밋·푸시한다.** 이번 유실의 직접 원인은 커밋 전 설치였다.
