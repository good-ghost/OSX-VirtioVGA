# 조사: A 방식 — 보조 kernel collection으로 kext 로드 (2026-11-24)

T-04 확정(PHASE-PLAN-02)에 따라, IOFramebuffer 하위 클래스를 올리는 방법으로 `/Library/Extensions` 설치(보조 kernel collection, AuxKC)를 조사했다. 이 방식은 확정된 결정 **Q-01(SIP 유지, OpenCore 주입만)** 을 바꿔야 하므로, 이 문서는 결정 전 근거 자료다.

## 1. 이 VM에서 확인한 사실

| 항목 | 값 | 확인 방법 |
| ---- | -- | --------- |
| kernel collection 구성 | 부트: IOPCIFamily, AppleVirtIO 등 / 시스템: **IOGraphicsFamily, IONDRVSupport** / 보조: AppleMobileDevice, HighPointRR, HighPointIOP | `kmutil inspect -B <kc>` |
| 보조 collection 파일 | `/Library/KernelCollections/AuxiliaryKernelExtensions.kc` 있음 (서드파티 kext 경로가 이미 쓰이고 있음) | `ls` |
| 대기 중인 kext | `/Library/StagedExtensions/Library/Extensions/VMwareGfx.kext` (VMware 서명, 보조 collection에는 없음). 예전에 설치했다가 승인·로드되지 않은 것으로 보인다 (확인 못 함) | `ls`, `codesign -dv` |
| SIP | enabled, `csr-active-config` = `00000000`, Authenticated Root enabled | `csrutil status`, `nvram` |
| 우리 kext 링크 | IOFramebuffer 심볼 98개를 시스템 collection의 IOGraphicsFamily에서 해석 | `kmutil libraries -p` |
| OC 보안 설정 (`~/temp/EFI` 2026-11-13 사본 기준, **현재 EFI와 다를 수 있음**) | `SecureBootModel=Disabled`, `ApECID=0`, `Vault=Optional`, `NVRAM > WriteFlash=true` | PlistBuddy |
| OC의 `csr-active-config` 관리 | `NVRAM > Add`에 `00000000`이 있지만 **`NVRAM > Delete`에는 없다** (boot-args, ForceDisplayRotationInEFI만 있음). 그래서 config 값을 바꿔도 이미 NVRAM에 있는 값이 덮어쓰이지 않을 수 있다 | 같은 사본 |

## 2. 외부 자료로 확인한 동작

- macOS 11부터 서드파티 kext는 필요할 때 바로 로드되지 않는다. **보조 collection(AuxKC)에 합쳐진 뒤 부팅 때 로드**되며, 바뀐 내용은 재부팅해야 반영된다. AuxKC를 다시 만들 때 사용자 승인이 필요하다 ([Apple Platform Security: Securely extending the kernel](https://support.apple.com/guide/security/sec8e454101b), [Apple Deployment: kernel extensions](https://support.apple.com/guide/deployment/depa5fb8376f)).
- SIP가 켜져 있으면 AuxKC에 넣을 kext마다 서명을 검사한다. **SIP를 끄면 kext 서명을 강제하지 않는다** ([Apple Deployment](https://support.apple.com/guide/deployment/depa5fb8376f), [Apple Developer Forums 661240](https://developer.apple.com/forums/thread/661240)).
- 서명 검사와 별개로 **사용자 승인(kext consent)** 이 따로 있다. SIP를 꺼도 승인 요구가 남는 사례가 있다 (Apple silicon 사례: [Apple Community](https://discussions-cn-prz.apple.com/thread/256062463)). 승인 거부 시 `KMErrorDomain Code=27 "not approved to load"`가 난다 ([Apple Developer Forums 711765](https://developer.apple.com/forums/thread/711765)).
- `csr-active-config`는 리틀 엔디언으로 저장된다. 예: `CSR_ALLOW_UNAPPROVED_KEXTS`(0x200)는 `00020000` ([hackintosh-forum.de](https://hackintosh-forum.de/forum/thread-pdf-export/52504)). Big Sur 이후 SIP 전체 해제 값은 `FF0F0000` ([Dortania Big Sur](https://dortania.github.io/OpenCore-Install-Guide/extras/big-sur)).
- 잘못된 kext가 매 부팅 로드될 때: kext 파일을 지우고 collection에서 빼야 하며, `kmutil clear-staging`, Recovery에서 `kmutil trigger-panic-medic`를 쓸 수 있다 ([Apple Developer Forums 804821](https://developer.apple.com/forums/thread/804821), `kmutil help`).
- 관리 도구: `kmutil rebuild`(AuxKC 다시 만들기), `kmutil showloaded --collection aux` ([Eclectic Light: Managing kernel extensions](https://eclecticlight.co/2024/12/03/managing-kernel-extensions/)).

"Reduced Security" 설정은 Apple silicon·T2 Mac의 보안 부팅 정책이다. 이 VM은 OpenCore `SecureBootModel=Disabled`(사본 기준)라 해당 정책의 제약은 받지 않을 것으로 보지만 확인하지 않았다.

## 3. 제안 절차 (시험안, 모두 사용자 승인 후)

SIP 비트는 필요한 최소만 끈다.

| 비트 | 이름 | 이유 |
| ---- | ---- | ---- |
| 0x1 | `CSR_ALLOW_UNTRUSTED_KEXTS` | 서명 없는 kext를 AuxKC에 넣기 위해 |
| 0x200 | `CSR_ALLOW_UNAPPROVED_KEXTS` | 서명 없는 kext는 개발자(Team ID) 단위 승인 화면에 나타나지 않을 수 있어 승인 단계를 건너뛰기 위해 (필요 여부 미확인) |

→ `csr-active-config` = 0x201 = 바이트 `01 02 00 00`. 이 값으로 충분한지는 시험으로 확인한다. 부족하면 원인을 보고 비트를 하나씩 추가한다 (전체 해제 `FF0F0000`는 쓰지 않는다).

1. **스냅샷**: VM을 끄고 호스트 스냅샷.
2. **OpenCore config** (EFI 마운트, 백업 후):
   - `NVRAM > Add > 7C436110-… > csr-active-config` = `01020000`
   - `NVRAM > Delete > 7C436110-…`에 `csr-active-config` 추가 (config 값이 매 부팅 적용되도록)
   - `Kernel > Add`의 VirtioVideo 항목 `Enabled=false` (OC 주입 중단. 같은 kext가 두 경로로 올라가지 않게)
   - 현재 EFI의 `SecureBootModel` 값을 실제로 확인
3. 재부팅 → `csrutil status`가 "Custom Configuration"이고 Kext Signing이 꺼졌는지 확인.
4. **kext 설치** (sudo, 사용자가 터미널 탭에서 비밀번호 입력):
   - `/Library/Extensions/VirtioVideo.kext`로 복사, 소유자 `root:wheel`
   - `kmutil load -p /Library/Extensions/VirtioVideo.kext` 또는 `kmutil rebuild`로 AuxKC에 넣기 (어느 쪽이 맞는지 시험에서 확정)
   - 승인 화면이 뜨면 시스템 설정 > 개인정보 보호 및 보안에서 사용자가 승인
5. 재부팅 → `kmutil showloaded --collection aux`에 VirtioVideo, `ioclasscount VirtioVideoFramebuffer` 확인.

**첫 시험은 지금의 0.0.21(personality 없는 스텁)으로 한다.** 화면을 건드리지 않으므로, 실패해도 "로드 경로"만 실패한다. 로드 경로가 확인된 뒤에 WI-2.005 본 구현(personality, takeover)을 올린다.

## 4. 되돌리는 방법

| 상황 | 방법 |
| ---- | ---- |
| 부팅은 되지만 드라이버를 끄고 싶음 | boot-arg `-vvideooff` (kext 코드가 확인, 지금처럼 동작) |
| AuxKC kext 때문에 부팅 불가 | **안전 모드**(boot-arg `-x` 또는 부팅 시 Shift): 서드파티 kext를 로드하지 않는다. 그 뒤 kext 삭제 + `kmutil rebuild` |
| 위가 안 될 때 | Recovery 터미널에서 `kmutil trigger-panic-medic --volume-root /Volumes/<볼륨>` |
| SIP 원복 | OC `csr-active-config`를 `00000000`으로 (Delete에 넣어 두었으므로 다음 부팅 적용) |
| 최후 | 호스트 스냅샷 복원 |

OC 주입 때와 달리 "config에서 Enabled=false"로 끄는 방법은 없다. 안전 모드가 그 역할을 한다.

## 5. 영향

- **보안**: 서명 없는 kext가 시스템 전체에서 허용된다. 이 VM은 개인 사용(Q-06)이고, 다른 서드파티 kext도 같은 조건으로 올라갈 수 있다는 점을 감수해야 한다. 나머지 SIP 보호(파일 시스템, 디버깅 등)는 유지한다.
- **개발 반복**: 변경마다 sudo 복사 + AuxKC 재생성 + 재부팅. 지금(EFI 마운트 + 재부팅)과 횟수는 비슷하다. AuxKC 재생성 후 승인 화면이 매번 뜨는지는 시험으로 확인한다.
- **로드 시점**: AuxKC는 부팅 중 시스템 collection 다음에 로드된다. 지금 OC 주입 kext도 IOGraphicsFamily 뒤에 로드되고 있었으므로(T-04) 부트 화면 넘겨받기(R-06)의 조건은 크게 다르지 않을 것으로 본다 (확인 필요).
- **문서·요구사항**: 승인되면 REQUIREMENT Q-01, §7-3, REQ-070/071/073, INSTALL.md를 바꾼다.

## 8. 시험 결과 (2026-11-24, kext 0.0.21) — 성공

| 단계 | 결과 |
| ---- | ---- |
| OC 설정 | `csr-active-config`=`01020000`, `NVRAM > Delete`에 `csr-active-config` 추가, `Kernel > Add:8` VirtioVideo `Enabled=false`. 현재 EFI `SecureBootModel=Disabled` 확인. 백업 `config.plist.bak-20261124-auxkc-sip` |
| 재부팅 후 SIP | `csrutil status`: Custom Configuration, **Kext Signing: disabled**, 나머지(Filesystem, Debugging, DTrace, NVRAM, BaseSystem) enabled. `Kernel Extension User Consent: DISABLED` (`spctl kext-consent status`) |
| 설치 | `sudo ditto` → `/Library/Extensions/VirtioVideo.kext`, `sudo chown -R root:wheel` |
| `sudo kmutil load -p …` | **Code 27 "not approved to load"**. syspolicyd 로그는 `Kernel Extension ALLOWED`였지만 AuxKC 재생성(kernelmanagerd)이 승인 대기(`(null)` Team ID)로 거절. 대기 중이던 VMwareGfx도 같은 요청에 묶였다 |
| 시스템 설정 > 개인정보 보호 및 보안 > 허용 | 사용자가 눌렀지만 다시 실행해도 Code 27. 서명 없는 kext는 Team ID 단위 승인 목록에 들어가지 못하는 것으로 본다 (추정) |
| `sudo kmutil load -z -p …` (`--no-authorization`) | **Code 28 "requires a reboot"**, AuxKC 재생성(933,888 → 1,032,192바이트, 05:11). `kmutil inspect`에 `local.virtiovideo.VirtioVideo 0.0.21` 포함. VMwareGfx는 포함되지 않음 |
| 재부팅 후 | `kmutil showloaded --collection aux`에 0.0.21 로드, **IOGraphicsFamily(599)·IOPCIFamily에 링크**. `ioclasscount`: VirtioVideoFramebuffer 0(클래스 등록됨, 인스턴스 없음), VirtioGpuDevice 1, VirtioVideoProbe 1. `driver-ok`(0x0F), `0:1280x800`, 물리 연속 프레임버퍼, 자체 시험 pass, MSI-X, 새 패닉 없음. 화면 1280×800(IONDRV) |

결론: **보조 collection으로 IOFramebuffer 하위 클래스를 가진 kext를 올릴 수 있다.** 필요한 것은 SIP 0x201과 `kmutil load -z`이며, 시스템 설정 승인은 서명 없는 kext에 효과가 없었다. 처음 제안의 "0x200으로 승인 단계를 건너뛴다"는 syspolicyd 판정에는 적용됐지만 AuxKC 재생성 단계에는 적용되지 않아 `-z`가 필요했다.

## 6. 확인하지 못한 것 (시험 전 기준)

- 0x201로 충분한지, 승인 화면이 뜨는지 (시험 필요)
- 현재 EFI의 `SecureBootModel`·`csr-active-config` 실제 값 (사본만 봄)
- `kmutil load -p`와 `kmutil rebuild` 중 이 버전에서 맞는 명령
- VMwareGfx.kext가 대기 상태로 남은 이유

## 7. 검토한 다른 방법

| 방법 | 판단 |
| ---- | ---- |
| A'. virtio 부분은 OC 주입, 프레임버퍼만 AuxKC | AuxKC를 만들 때 디스크의 collection을 기준으로 링크하는데, OC로 주입한 kext는 디스크의 부트 collection에 없다. 우리 kext끼리 의존하면 링크할 수 없을 가능성이 높다 (확인 못 함). 한 kext 전체를 AuxKC로 옮기는 쪽이 단순하다 |
| B. IOGraphicsFamily를 부트 collection에 함께 주입 | 실행 파일이 디스크·KDK 어디에도 없다. 불가 (2026-11-24 정정: KDK에는 `IOGraphicsFamily.kext/IOGraphicsFamily`(번들 최상위)와 `IONDRVSupport.kext/Contents/MacOS/IONDRVSupport` 실행 파일이 있다. 처음 조사 때 `Contents/MacOS`만 보고 잘못 적었다) |
| C. Lilu 방식 런타임 해석 | vtable 직접 구성 필요. 비권장 |
| Apple 서명(Developer ID + kext 권한) | 서명 인증서가 있으면 SIP를 유지할 수 있다. 개인 사용(Q-06)에서는 인증서 발급 부담이 크다 |
