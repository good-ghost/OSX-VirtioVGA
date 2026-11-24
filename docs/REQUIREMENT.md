# Virtio-Video 요구사항 정의서

## 1. 문서 정보

| 항목      | 내용                                                         |
| --------- | ------------------------------------------------------------ |
| 문서      | `docs/REQUIREMENT.md`                                        |
| 버전      | 1.0                                                          |
| 작성일    | 2026-11-23                                                   |
| 상태      | 확정 — 모든 Open Question 답변 완료. 이후 변경은 변경 이력에 남긴다 |
| 대상      | OSX-KVM 게스트 macOS용 virtio-gpu 2D 프레임버퍼 드라이버(kext) |

### 1.1 변경 이력

| 버전 | 날짜       | 구분 | 내용      |
| ---- | ---------- | ---- | --------- |
| 0.1  | 2026-11-23 | ADD  | 최초 작성 |
| 0.2  | 2026-11-23 | ENHANCE | 사용자 지시 "Use the recommendations"에 따라 Q-01·02·03·05·06·07·09·10을 권장안으로 확정. REQ-036·040·061·073, NFR-008, §4.2, §7, §9.2 반영. Q-04·08은 정보 요청이라 열어 둠 |
| 0.3  | 2026-11-23 | ENHANCE | 사용자가 준 호스트 정보(QEMU 10.0.13, `-device` 목록)로 §8.1 추가, HST-002·A-2 보완, Q-04 일부 답변 반영 |
| 0.4  | 2026-11-23 | ENHANCE | 사용자 캡처로 Q-08 답변. §2.5(배경화면 현상 측정) 추가, §2.4-3·AC-07·R-03 보완, Q-11 추가 |
| 0.5  | 2026-11-23 | ENHANCE | Q-11 시험 결과 반영: 사진 배경(Sonoma.heic)은 테두리 없이 표시, 메뉴 막대는 여전히 불투명. 사용자 보고(사진으로 설정했는데 단색 표시)와 디스플레이 UUID 관찰을 §2.5에 추가. REQ-025·AC-07 보완 |
| 0.6  | 2026-11-23 | ENHANCE | 사용자 보고(Sequoia 사진 → Cyan 단색 표시)와 Metal 장치 확인 결과로 원인 규명(§2.5.2). G-5·R-03 수정, Q-11 답변 완료, Q-12 추가 |
| 0.7  | 2026-11-23 | ENHANCE | 사용자가 준 `virtio-vga,help` 출력으로 §8.1 기본값 표 추가. 1280×800 원인 확정, HST-002·A-2·REQ-014·REQ-016·REQ-036 보완 |
| 0.8  | 2026-11-23 | ENHANCE | 사용자 정보: 디스플레이 백엔드 GTK, `-display gtk,gl=on`이면 `virtio-vga-gl` 사용 가능. §8.1.2 추가, 범위·REQ-007·REQ-027·HST-001·HST-004·Q-12 보완, Q-13 추가 |
| 0.9  | 2026-11-23 | ENHANCE | 사용자 지시 "Use recommendations"로 Q-12(A+B), Q-13(A) 확정. §4.2, §8.1.2, R-03 반영 |
| 1.0  | 2026-11-23 | ENHANCE | Q-04 답변(시스템 잠자기 사용 안 함, 일정 시간 후 로그인 화면으로 전환)으로 확정. REQ-053 우선순위 M, REQ-054 W, AC-09·R-09 추가, §8.1.3 추가 |

### 1.2 용어

| 용어            | 뜻                                                                                     |
| --------------- | -------------------------------------------------------------------------------------- |
| 게스트          | QEMU/KVM 위에서 실행되는 macOS (이 문서가 다루는 드라이버가 설치되는 쪽)               |
| 호스트          | OSX-KVM을 실행하는 Linux + QEMU                                                         |
| virtio-vga      | QEMU 장치. VGA 호환 레거시 프레임버퍼(BAR0)와 virtio-gpu 기능을 함께 가진다            |
| scanout         | virtio-gpu가 화면(모니터)으로 내보내는 출력 하나                                       |
| resource        | virtio-gpu가 관리하는 2D 이미지 버퍼. 게스트 메모리(backing)를 붙여 쓴다                |
| controlq / cursorq | virtio-gpu의 명령 큐(0번) / 커서 큐(1번)                                            |
| GOP             | UEFI Graphics Output Protocol. 부팅 시 OVMF가 설정하는 단일 해상도 프레임버퍼           |
| IONDRVFramebuffer | Apple `IONDRVSupport.kext`의 범용 프레임버퍼 드라이버. 전용 드라이버가 없을 때 GOP 화면을 그대로 쓴다 |
| OC              | OpenCore 부트로더                                                                      |
| KDK             | Apple Kernel Debug Kit. 커널 심볼과 디버그 커널                                        |

## 2. 배경과 현상 분석

### 2.1 확인한 게스트 환경 (2026-11-23, `scripts/collect-env.sh`와 같은 명령으로 수집)

| 항목              | 값                                                                 |
| ----------------- | ------------------------------------------------------------------ |
| macOS             | 15.8.3 (빌드 24H241), Sequoia                                      |
| 아키텍처          | x86_64, CPU 표시명 "Intel Core Processor (Skylake, IBRS, no TSX)"  |
| SMBIOS 모델       | iMac19,1                                                           |
| 메모리            | 32 GiB                                                             |
| SIP               | enabled, `csr-active-config` = `00000000`                          |
| boot-args         | `keepsyms=1`                                                       |
| 로드된 서드파티 kext | Lilu 1.6.8, WhateverGreen 1.6.7 (OpenCore 주입으로 추정 — Q-01)  |
| Xcode             | 26.3 (17C529), SDK `MacOSX26.2.sdk`                                |
| 커널 헤더         | SDK `Kernel.framework`에 `IOKit/graphics/IOFramebuffer.h`, `IOKit/pci/IOPCIDevice.h` 존재 |
| KDK               | 설치되어 있지 않음 (`/Library/Developer/KDKs` 없음)                |

### 2.2 디스플레이 장치

| 항목              | 값                                                                  |
| ----------------- | ------------------------------------------------------------------- |
| PCI 위치          | 07:00.0 (PCIe 루트 포트 아래, 링크 x1)                             |
| Vendor / Device   | `0x1af4` / `0x1050` (virtio 장치 타입 16 = GPU, modern 전용 ID)     |
| Revision          | `0x01`                                                              |
| Class code        | `0x030000` (VGA 호환) → QEMU `virtio-vga`. `virtio-gpu-pci`였다면 `0x038000` |
| Subsystem         | `0x1af4` / `0x1100`                                                 |
| BAR0              | 메모리 `0x80000000`, 8 MiB — VGA/GOP 프레임버퍼                     |
| BAR2              | 메모리 `0x81600000`, 16 KiB, prefetchable — virtio modern capability 영역으로 추정 |
| BAR4              | 메모리 `0x81000000`, 4 KiB — MSI-X 테이블로 추정                    |
| Expansion ROM     | `0x80800000`, 64 KiB                                                 |

BAR 해석은 ioreg `assigned-addresses` 인코딩을 읽은 것이다. 각 BAR의 실제 역할(common/notify/isr/device cfg 위치)은 Phase 1에서 PCI capability 목록을 읽어 확정한다.

### 2.3 현재 드라이버 상태

- 디스플레이 장치에 붙은 서비스
  - `.Display_boot` — `IONDRVFramebuffer` (`com.apple.iokit.IONDRVSupport`), `IONameMatch "display"`, `IOProbeScore 20000`, `IOMatchCategory IOFramebuffer`
  - `AppleVirtIOPCITransport` (`com.apple.driver.AppleVirtIO` 230.100.3) — 모든 `0x1af4` 장치에 붙는다. GPU용 클라이언트가 없어 하위 서비스는 없다.
- 모드 목록: `IOFBConfig.IOFBModes`에 ID 100 하나(1280×800)만 있다.
- `IOFBMemorySize` = 4,096,000 = 1280 × 800 × 4 바이트.
- `system_profiler`: "VRAM (Total): 3 MB", "Kernel Extension Info: No Kext Loaded".
- `AppleParavirtGPU.kext`는 `IOPCIMatch 0xEEEE106B`(Apple Virtualization.framework 전용 장치)만 매칭하므로 이 장치에는 쓸 수 없다.

### 2.4 원인

1. **VRAM 3 MB 표시**: 실제 비디오 메모리 한계가 아니다. 범용 드라이버가 GOP 프레임버퍼 크기(4,096,000 바이트 ≈ 3.9 MiB)를 `IOFBMemorySize`로 보고하고, 이것이 MB 단위로 내림되어 "3 MB"로 보인다.
2. **해상도 고정**: `IONDRVFramebuffer`는 펌웨어가 정한 모드 하나만 안다. 모드 전환, EDID, 호스트 창 크기 변경을 처리할 수단이 없다.
3. **배경화면 등 표시 이상**: 측정과 원인 확인은 §2.5에 있다. 게스트에는 Metal 장치가 없다(`MTLCopyAllDevices()` = 빈 목록). Sequoia 배경은 Metal로 그리는 확장이라 표시되지 못하고 Cyan 단색으로 대체된다. 메뉴 막대·Dock이 불투명한 것도 같은 원인으로 추정한다. **2D 드라이버로는 해결되지 않는다** (R-03, Q-12).

### 2.5 배경화면 현상 측정 (2026-11-23, Q-08)

근거: 사용자 캡처 `docs/screenshots/before/2026-11-23-desktop-1280x800.png` (1280×800)를 픽셀 단위로 측정했고, 설정은 `osascript`, `defaults`, `~/Library/Application Support/com.apple.wallpaper/Store/Index.plist`로 읽었다.

| 항목                 | 측정값                                                                                     |
| -------------------- | ------------------------------------------------------------------------------------------ |
| 설정된 배경 그림     | `/System/Library/Desktop Pictures/Solid Colors/Cyan.png` (128×128 단색, 모든 데스크톱 동일) |
| 배경 그림이 그려진 영역 | x 16~1263, y 40~783 — 메뉴 막대(24 px) 아래부터 사방 16 px씩 안쪽                       |
| 테두리               | 사방 16 px, 색 RGB(0,124,143) — 배경색보다 어두운 청록                                     |
| 안쪽 배경색          | RGB(0,188,217). 원본 파일 RGB(20,175,208)과의 차이는 색 관리 변환으로 보인다(확인 못 함)   |
| 메뉴 막대            | y 0~23, 불투명 단색 RGB(218,218,218). 배경색이 비치지 않는다                               |
| Dock 배경            | 불투명 단색 RGB(200,200,200)                                                                |
| 투명도 줄이기 설정   | `com.apple.universalaccess reduceTransparency` 키 없음 (사용자가 켜지 않음)                |

해석:

- 배경 그림 자체는 지정한 색으로 그려진다. 이상한 점은 **사방 16 px 테두리**와 **투명 효과 없음**이다.
- 사용자가 투명도 줄이기를 켜지 않았는데 메뉴 막대와 Dock이 불투명하다. 시스템이 GPU 가속이 없다고 보고 투명·블러 효과를 끈 것으로 추정한다.
- 테두리가 생기는 원인은 확인하지 못했다. 가속이 없을 때의 WindowServer/WallpaperAgent 대체 경로, 또는 디스플레이 정보(EDID·물리 크기 없음)에서 오는 배치 문제를 후보로 둔다. 2D 드라이버가 EDID와 모드 정보를 제공한 뒤 테두리가 사라지는지 확인한다.
- 사용자 보고(2026-11-23): **System Settings에서는 사진 배경으로 설정했는데 단색으로 표시된다.** 그러나 이 시점에 배경 저장소(`Index.plist`)에 기록된 값은 이미 `Solid Colors/Cyan.png`였다. 즉 사용자가 고른 사진이 저장소에 남지 않았거나, 시스템이 단색으로 대체해 저장한 것으로 보인다. 어느 쪽인지는 확인하지 못했다.
- 배경 저장소의 디스플레이 키가 `FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF`다. 정상적인 모니터라면 EDID의 제조사·제품·일련번호로 만든 UUID가 들어간다. 지금은 디스플레이를 식별할 정보가 없다는 뜻으로 보이며, 배경 설정이 저장·적용되지 않는 현상과 관련 있을 수 있다 (추정).

#### Q-11 시험 (2026-11-23, 사용자 허락 후 진행)

`osascript`로 배경을 `/System/Library/Desktop Pictures/Sonoma.heic`로 바꾸고 4초 뒤 `screencapture`로 캡처한 다음, 원래 저장소 값인 `Solid Colors/Cyan.png`로 되돌렸다. 캡처: `docs/screenshots/before/2026-11-23-desktop-sonoma-1280x800.png`.

| 항목       | 결과                                                                                  |
| ---------- | ------------------------------------------------------------------------------------- |
| 적용 결과  | `get picture`가 `/System/Library/CoreServices/DefaultDesktop.heic`를 돌려줌 (시스템이 경로를 바꿔 기록) |
| 표시       | 사진이 화면 끝(x 0, y 799)까지 그려짐. **16 px 테두리 없음**                          |
| 메뉴 막대  | 배경과 관계없이 RGB(218,218,218) 불투명 — 투명 효과 없음은 그대로                     |

결론:

- 사진 파일을 직접 지정하면 정상적으로 그려진다. 따라서 "사진이 단색으로 보이는" 현상은 렌더링 실패라기보다 **선택한 배경이 저장·적용되는 과정의 문제**일 가능성이 높다. 후보는 ① 디스플레이 식별 정보 없음(UUID `FFFF…`) ② 사용자가 고른 배경이 다운로드형(`.madesktop`, Aerial/동적 배경)이라 GPU 없이 재생하지 못해 단색으로 대체됨. 둘 다 확인하지 못했다.
- 16 px 테두리는 단색 그림(Cyan.png)을 표시할 때만 나타났다. 드라이버 문제라기보다 단색 배경의 표시 방식일 수 있다.
- 메뉴 막대·Dock 불투명은 배경 종류와 관계없이 나타난다. GPU 가속 부재 추정(R-03)과 일치한다.

#### 2.5.2 원인 확인: Sequoia 배경은 Metal로 그린다 (2026-11-23)

사용자 보고: **System Settings에서 "Sequoia" 배경을 고르면 Cyan 단색으로 표시된다.**

| 확인 항목 | 결과 |
| --------- | ---- |
| Sequoia 배경 파일 | `/System/Library/Desktop Pictures/`에 Sequoia 이미지 파일이 없다. 배경은 `/System/Library/ExtensionKit/Extensions/WallpaperSequoiaExtension.appex`가 실시간으로 그린다 |
| 확장의 Metal 사용 | `WallpaperSequoiaExtension.appex`(그리고 `WallpaperMacintoshExtension.appex`)의 Resources에 `default.metallib`(Metal 셰이더)가 있다 |
| 게스트의 Metal 장치 | `MTLCreateSystemDefaultDevice()` = `nil`, `MTLCopyAllDevices()` = `[]` — **Metal 장치가 없다** |
| 저장소 값 | Sequoia를 골랐는데 `Index.plist`에는 `Solid Colors/Cyan.png`가 기록돼 있었다 |
| 비교 | 일반 이미지 파일(`Sonoma.heic`)은 Metal 없이도 정상 표시된다 (§2.5 Q-11 시험) |

결론:

- **사진이 단색으로 보이는 원인은 Metal 장치가 없어서 Sequoia 배경 확장이 그림을 그리지 못하고, 시스템이 Cyan 단색으로 대체하기 때문이다.** 대체 과정 자체를 로그로 보지는 못했다(최근 3시간 로그에 관련 기록 없음). 다만 위 네 가지 사실이 모두 이 설명과 맞는다.
- 이 프로젝트의 2D 프레임버퍼 드라이버는 Metal 장치를 만들지 않는다 (§4.2 Out-of-Scope). **따라서 이 드라이버로는 Sequoia 배경 문제가 해결되지 않는다.** 메뉴 막대·Dock 투명 효과도 같은 이유로 해결되지 않을 가능성이 높다.
- 지금 쓸 수 있는 우회책: 정적 이미지 배경(`.heic`, `.jpg`, `.png` — 예: `Sonoma.heic`, `iMac *.heic`, 직접 넣은 사진)을 쓴다. Metal을 쓰는 확장형 배경(Sequoia, Macintosh 등)과 다운로드형 `.madesktop` 중 동적·영상 배경은 표시되지 않을 수 있다.
- Metal 지원은 Apple이 공개하지 않은 GPU 드라이버 인터페이스가 필요하다. 별도 프로젝트 수준의 일이라 이번 범위에 넣지 않는다 (Q-12).

### 2.6 드라이버 없이 가능한 완화책 (참고)

드라이버 개발과 별개로, OpenCore `UEFI > Output > Resolution` 또는 OVMF 해상도 설정으로 GOP 해상도를 올리면 `IOFBMemorySize`도 비례해서 커진다. 다만 GOP 프레임버퍼는 BAR0(현재 8 MiB) 안에 있어야 하므로 32bpp 기준 1920×1080(7.9 MiB)이 상한이고, 런타임 해상도 변경은 여전히 안 된다. 이 프로젝트의 드라이버는 게스트 RAM을 backing으로 쓰므로 BAR0 크기 제약을 받지 않는다.

## 3. 목표

| ID  | 목표                                                                         |
| --- | ---------------------------------------------------------------------------- |
| G-1 | macOS 안에서 여러 해상도를 선택하고 런타임에 바꿀 수 있다                     |
| G-2 | 호스트 창 크기·EDID에 맞춘 해상도를 자동으로 제공한다                         |
| G-3 | VRAM이 드라이버가 실제로 관리하는 프레임버퍼 메모리 크기로 표시된다          |
| G-4 | 드라이버가 실패해도 기존 부트 프레임버퍼로 돌아가 VM이 계속 부팅된다         |
| G-5 | 배경화면 문제의 원인을 밝히고 문서화한다 — 완료(§2.5.2: Metal 장치 부재). 드라이버 적용 후에도 결과가 같은지 AC-07로 기록한다 |

## 4. 범위

### 4.1 In-Scope

- macOS 15 (Sequoia) x86_64 게스트용 IOKit kext (C++, `Kernel.framework`)
- QEMU `virtio-vga` 장치(`1af4:1050`, class `0x030000`). 호환 범위로 `virtio-gpu-pci`(class `0x038000`)와 `virtio-vga-gl`(같은 ID, 3D 기능은 쓰지 않고 2D 경로로만 동작 — REQ-007)
- virtio 1.x modern PCI 전송 계층(게스트 쪽 자체 구현)
- virtio-gpu **2D** 명령 세트, EDID, 하드웨어 커서
- `IOFramebuffer` 하위 클래스: 모드 목록, 모드 전환, 커서, 감마(수용만), 전원 상태
- OpenCore 주입 방식 설치·롤백 절차와 보조 스크립트
- 상태 확인용 사용자 공간 도구(IORegistry 조회)
- 개발 환경: 빌드, 디버그(KDK, QEMU gdbstub), 로그 수집 절차

### 4.2 Out-of-Scope

- 3D 가속: virgl(`virtio-vga-gl`의 3D 기능), Venus/Vulkan, Metal·OpenGL 드라이버, `IOAccelerator`. `virtio-vga-gl` 장치 자체는 2D 호환 대상이다
- Metal 가속(Sequoia 등 확장형 배경, 투명 효과 포함)은 이번 범위 밖이다. 2D 드라이버 완료 후 별도 프로젝트로 가능성을 조사한다 (Q-12 확정)
- 비디오 디코드/인코드 가속 (`virtio-video` 장치 타입과는 무관 — 프로젝트 이름의 "Video"는 디스플레이를 뜻한다)
- Apple Silicon 게스트, Apple Virtualization.framework
- 호스트(QEMU) 코드 수정. 호스트 쪽은 실행 옵션 변경만 요구한다 (§8)
- DriverKit(dext) 구현: DriverKit에는 디스플레이/프레임버퍼 패밀리가 없다
- 공개 배포와 Apple kext 서명·공증 (Q-06 확정: 개인 사용. 안정화 후 GitHub 공개를 다시 검토)

## 5. 기능 요구사항

우선순위는 MoSCoW(M=Must, S=Should, C=Could, W=Won't this time).

### 5.1 장치 매칭과 초기화

| ID      | 요구사항                                                                                                  | 우선 |
| ------- | --------------------------------------------------------------------------------------------------------- | ---- |
| REQ-001 | `IOPCIDevice`를 provider로 하여 `IOPCIMatch 0x10501af4`로 매칭한다. class code로 virtio-vga/virtio-gpu-pci를 구분해 기록한다 | M |
| REQ-002 | `IOMatchCategory IOFramebuffer`에서 `IONDRVFramebuffer`(score 20000)보다 우선하도록 probe score를 정하고, 실제로 부트 프레임버퍼를 대체하는지 검증한다 | M |
| REQ-003 | 같은 장치에 붙는 `AppleVirtIOPCITransport`와 충돌하지 않음을 확인한다. 충돌하면 회피 방법(매칭 조정 등)을 설계 문서에 남긴다 | M |
| REQ-004 | `probe()`에서 장치 응답(virtio capability, feature, num_scanouts)을 확인하고, 조건을 만족하지 못하면 매칭을 거절해 `IONDRVFramebuffer`가 남게 한다 | M |
| REQ-005 | `start()` 중 어느 단계에서 실패해도 할당한 자원을 모두 해제하고 장치를 reset한 뒤 실패를 반환한다 | M |
| REQ-006 | `com.apple.iokit.IOGraphicsFamily`, `com.apple.iokit.IOPCIFamily` 등 의존성을 `OSBundleLibraries`에 명시한다 | M |
| REQ-007 | `virtio-vga-gl` 장치에서도 2D 드라이버로 동작한다. `VIRTIO_GPU_F_VIRGL`은 협상하지 않는다. 호스트 virgl 렌더러가 3D 미협상 게스트의 2D 명령을 처리하는지는 확인하지 못했으므로 Phase 2에서 시험한다 (Q-13) | S |

### 5.2 virtio modern PCI 전송 계층

| ID      | 요구사항                                                                                                  | 우선 |
| ------- | --------------------------------------------------------------------------------------------------------- | ---- |
| REQ-010 | PCI vendor capability(`cfg_type` 1~5)를 순회해 common/notify/ISR/device cfg 영역의 BAR·오프셋·길이를 찾는다. 범위가 BAR 크기를 넘으면 거절한다 | M |
| REQ-011 | 초기화 순서를 사양대로 지킨다: reset → ACKNOWLEDGE → DRIVER → feature 협상 → FEATURES_OK 재확인 → 큐 설정 → DRIVER_OK | M |
| REQ-012 | `VIRTIO_F_VERSION_1`을 필수로 협상한다. 없으면 거절한다                                                 | M |
| REQ-013 | split virtqueue를 구현한다. 디스크립터 테이블·avail·used 링은 `IOBufferMemoryDescriptor` + `IODMACommand`로 DMA 주소를 얻는다 | M |
| REQ-014 | MSI-X 인터럽트를 `IOFilterInterruptEventSource`/`IOInterruptEventSource`로 처리한다. 현재 장치는 벡터 3개(§8.1.1)이므로 config·controlq·cursorq에 하나씩 배정한다. MSI-X를 못 쓰면 레거시 INTx + ISR로 동작한다 | M |
| REQ-015 | 인터럽트 없이도 동작을 확인할 수 있도록 폴링 모드를 디버그 옵션으로 둔다                               | S |
| REQ-016 | `VIRTIO_F_RING_INDIRECT_DESC`, `VIRTIO_F_RING_EVENT_IDX`는 현재 장치가 제공한다(§8.1.1). 사용 여부는 성능 측정 후 정한다 | C |
| REQ-017 | 장치가 `DEVICE_NEEDS_RESET`을 보고하면 장치를 reset하고 현재 모드를 다시 설정한다                        | S |
| REQ-018 | 전송 계층은 virtio-gpu와 분리된 클래스로 둔다 (다른 virtio 장치에 재사용할 수 있게)                     | S |

### 5.3 virtio-gpu 2D 명령

| ID      | 요구사항                                                                                                  | 우선 |
| ------- | --------------------------------------------------------------------------------------------------------- | ---- |
| REQ-020 | `VIRTIO_GPU_CMD_GET_DISPLAY_INFO`로 scanout별 활성 여부와 선호 해상도를 읽는다                           | M |
| REQ-021 | `RESOURCE_CREATE_2D`, `RESOURCE_ATTACH_BACKING`, `SET_SCANOUT`, `TRANSFER_TO_HOST_2D`, `RESOURCE_FLUSH`, `RESOURCE_DETACH_BACKING`, `RESOURCE_UNREF`를 구현한다 | M |
| REQ-022 | 픽셀 형식은 macOS 32bpp(ARGB8888, 바이트 순서 BGRA)에 맞는 `VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM`(또는 A8 변형)을 쓴다. 색이 뒤바뀌지 않음을 화면으로 확인한다 | M |
| REQ-023 | 모든 명령의 응답 타입을 확인한다. `RESP_ERR_*`이면 명령 이름·인자와 함께 로그를 남기고 호출자에게 오류를 돌려준다 | M |
| REQ-024 | 명령 제출은 직렬화하고 응답 대기에 타임아웃을 둔다. 타임아웃 값은 상수로 정의한다                       | M |
| REQ-025 | `VIRTIO_GPU_F_EDID`를 협상하고 `GET_EDID`로 받은 EDID를 검증(길이, 체크섬)한 뒤 `IODisplay`에 제공한다. 적용 후 배경 저장소의 디스플레이 키가 `FFFFFFFF-…`가 아닌 고유 UUID가 되는지 확인한다 (§2.5) | S |
| REQ-026 | 장치 config의 `events_read`에서 `VIRTIO_GPU_EVENT_DISPLAY`를 받으면 display info를 다시 읽고 모드 목록을 갱신한다 | S |
| REQ-027 | `VIRTIO_GPU_F_VIRGL`, `VIRTIO_GPU_F_RESOURCE_BLOB`, `VIRTIO_GPU_F_CONTEXT_INIT` 등 3D/blob 관련 feature는 협상하지 않는다 | M |

### 5.4 프레임버퍼 메모리와 화면 갱신

| ID      | 요구사항                                                                                                  | 우선 |
| ------- | --------------------------------------------------------------------------------------------------------- | ---- |
| REQ-030 | 프레임버퍼는 게스트 RAM에서 할당하고(wired, page 정렬), scatter-gather 목록으로 resource에 붙인다. 물리 연속일 필요는 없다 | M |
| REQ-031 | `getApertureRange()`가 현재 모드의 프레임버퍼 메모리를 돌려준다. WindowServer가 이 메모리에 직접 그린다    | M |
| REQ-032 | IOFramebuffer에는 변경 영역 통지가 없으므로, 타이머로 `TRANSFER_TO_HOST_2D` + `RESOURCE_FLUSH`를 주기적으로 보낸다. 기본 주기 60 Hz, 설정으로 바꿀 수 있다 | M |
| REQ-033 | 화면이 바뀌지 않을 때 전송량을 줄이기 위해 타일 단위 변경 감지(비교 또는 해시)를 둔다. 효과는 측정 후 기본값을 정한다 | S |
| REQ-034 | 모드 전환 시 새 resource를 만들고 `SET_SCANOUT`으로 바꾼 뒤 이전 resource를 해제한다. 전환 중 커널 패닉이나 정지가 없어야 한다 | M |
| REQ-035 | 드라이버가 화면을 넘겨받을 때 부트 프레임버퍼 내용을 복사해 부팅 화면(Apple 로고·진행 막대)이 끊기지 않게 한다 | S |
| REQ-036 | 프레임버퍼 메모리 풀의 최대 크기를 설정값으로 둔다. 기본값은 128 MiB (Q-03 확정). 호스트 `max_hostmem`(256 MiB)을 넘지 않는다                          | M |

### 5.5 디스플레이 모드

| ID      | 요구사항                                                                                                  | 우선 |
| ------- | --------------------------------------------------------------------------------------------------------- | ---- |
| REQ-040 | 표준 모드 목록을 제공한다: 1024×768, 1280×800, 1280×1024, 1440×900, 1600×900, 1680×1050, 1920×1080, 1920×1200, 2560×1440, 2560×1600. 최대 해상도는 2560×1600 (Q-02 확정). 메모리 풀에 들어가지 않는 모드는 뺀다 | M |
| REQ-041 | display info의 선호 해상도와 EDID의 모드를 목록에 추가하고, 선호 해상도를 기본 모드로 표시한다           | S |
| REQ-042 | 시스템 설정 > 디스플레이에서 모드를 바꾸면 화면이 그 해상도로 바뀐다 (`setDisplayMode`)                  | M |
| REQ-043 | 호스트 창 크기가 바뀌면(REQ-026) 그 크기의 모드를 동적으로 추가하고, 설정에 따라 자동 전환한다           | C |
| REQ-044 | HiDPI(Retina) 배율 모드(예: 2560×1600을 1280×800 UI로)를 제공한다                                        | S |
| REQ-045 | 마지막으로 선택한 모드를 다음 부팅에서 복원한다 (macOS 디스플레이 환경설정 저장 방식과 호환)             | S |
| REQ-046 | 다중 scanout(여러 모니터)을 지원한다. `num_scanouts`만큼 `IOFramebuffer` 인스턴스를 만든다               | C |

### 5.6 커서, 감마, 전원

| ID      | 요구사항                                                                                                  | 우선 |
| ------- | --------------------------------------------------------------------------------------------------------- | ---- |
| REQ-050 | `setCursorImage`/`setCursorState`를 cursorq의 `UPDATE_CURSOR`/`MOVE_CURSOR`로 구현한다 (64×64 ARGB)      | S |
| REQ-051 | 하드웨어 커서를 쓸 수 없으면 소프트웨어 커서로 동작한다 (드라이버가 하드웨어 커서 미지원을 보고)         | M |
| REQ-052 | `setGammaTable`은 값을 받아 저장하고 성공을 돌려준다 (virtio-gpu 2D에는 감마 명령이 없다)                | M |
| REQ-053 | 디스플레이 잠자기(현재 `displaysleep 10`분)와 깨우기 시 flush 타이머를 멈추고 재개한다. 깨어난 뒤 잠금(로그인) 화면이 정상 표시되어야 한다 (§8.1.3) | M |
| REQ-054 | 시스템 잠자기/깨우기 후 장치를 다시 초기화하고 모드를 복원한다 — Q-04 확정(시스템 잠자기 사용 안 함)으로 이번 범위에서 제외. 다만 전원 상태 변경 요청에 패닉 없이 응답하는 최소 처리는 REQ-005·NFR-001 범위에서 유지한다 | W |

### 5.7 VRAM 표시와 상태 보고

| ID      | 요구사항                                                                                                  | 우선 |
| ------- | --------------------------------------------------------------------------------------------------------- | ---- |
| REQ-060 | `IOFBMemorySize`를 프레임버퍼 메모리 풀 크기로 보고한다                                                  | M |
| REQ-061 | `system_profiler SPDisplaysDataType`의 "VRAM (Total)"이 REQ-060 값과 같게 표시되도록 필요한 레지스트리 속성을 조사해 설정한다. 어떤 속성을 읽는지는 아직 확인하지 못했다 | M |
| REQ-062 | `system_profiler`의 "Kernel Extension Info"에 이 드라이버가 표시된다                                     | S |
| REQ-063 | 드라이버 상태(협상한 feature, scanout 수, 현재 모드, 큐 통계, 오류 수)를 IORegistry 속성으로 공개한다    | S |
| REQ-064 | 로그는 `os_log` 계열(또는 `IOLog`)로 남기고 수준(오류/정보/디버그)을 boot-arg 또는 Info.plist 설정으로 고른다 | M |

### 5.8 설치, 롤백, 도구

| ID      | 요구사항                                                                                                  | 우선 |
| ------- | --------------------------------------------------------------------------------------------------------- | ---- |
| REQ-070 | OpenCore `Kernel > Add` 주입 방식 설치 절차를 문서로 제공한다 (config.plist 항목, `MinKernel`, 순서)      | M |
| REQ-071 | 롤백 절차를 문서로 제공한다: OC 부팅 항목에서 kext 비활성화, 호스트 디스크 스냅샷 복원                    | M |
| REQ-072 | 드라이버를 끄는 boot-arg(예: `-vvideooff`)를 둔다. 이 값이 있으면 `probe()`에서 거절한다                  | M |
| REQ-073 | 개발 중 빠른 반복을 위한 `kmutil load` 방식 절차를 문서화한다 — Q-01 확정(OpenCore 주입만, SIP 유지)으로 이번 범위에서 제외 | W |
| REQ-074 | `tools/`에 상태 조회 CLI를 둔다 (IORegistry 속성 읽기만, user client 없음)                               | C |
| REQ-075 | `scripts/collect-env.sh`로 적용 전/후 환경을 같은 형식으로 수집한다                                       | M |

## 6. 비기능 요구사항

| ID      | 분류     | 요구사항                                                                                        |
| ------- | -------- | ----------------------------------------------------------------------------------------------- |
| NFR-001 | 안정성   | 24시간 연속 사용(일반 데스크톱 작업, 동영상 재생)에서 커널 패닉 0회                             |
| NFR-002 | 안정성   | 모드 전환 100회 반복에서 패닉·화면 정지 0회, 메모리 누수 없음(`zprint`/IORegistry 통계로 확인) |
| NFR-003 | 안정성   | kext 언로드 후 재로드가 성공한다 (개발 중 반복용). 언로드 시 모든 자원을 해제한다               |
| NFR-004 | 성능     | 1920×1080 60 Hz flush에서 게스트 CPU 사용률 증가분을 측정해 기록한다. 목표: 한 코어의 10% 이하 (측정 후 조정) |
| NFR-005 | 성능     | 마우스 이동·창 드래그 시 체감 지연이 현재(IONDRV)보다 나쁘지 않다. 녹화로 비교한다              |
| NFR-006 | 보안     | 호스트가 준 값(응답 길이, `num_scanouts` ≤ 16, EDID 길이 ≤ 1024, 해상도 상한)을 모두 범위 검사한다 |
| NFR-007 | 보안     | user client를 만들지 않는다. 만들어야 하면 entitlement 검사와 입력 검증을 함께 설계한다         |
| NFR-008 | 호환성   | 대상 macOS 15.x, 시험 기준 15.8.3(24H241). macOS 14 이하는 지원하지 않는다 (Q-05 확정)           |
| NFR-009 | 호환성   | Lilu/WhateverGreen이 로드된 상태에서 충돌하지 않는다. WhateverGreen이 이 장치를 건드리는지 확인한다 |
| NFR-010 | 유지보수 | 커널 의존 없는 로직(모드 계산, EDID 파싱, 타일 비교, virtqueue 인덱스 계산)은 사용자 공간 단위 테스트로 검증한다 |
| NFR-011 | 유지보수 | 매직 넘버를 두지 않는다. virtio/virtio-gpu 상수는 사양 절 번호를 주석으로 단 헤더에 모은다      |
| NFR-012 | 라이선스 | GPL 코드(Linux virtio-gpu, QEMU)를 복사하지 않는다. 동작 이해용 참고만 하고 출처를 문서에 남긴다 |
| NFR-013 | 관찰성   | 패닉 시 원인을 찾을 수 있도록 `keepsyms=1` 유지, 주요 상태 전이를 로그로 남긴다                 |

## 7. 기술 제약과 설계 방침

1. **kext만 가능하다.** DriverKit에는 `IOFramebuffer`에 해당하는 패밀리가 없다. 서드파티 디스플레이 드라이버는 `IOGraphicsFamily`의 `IOFramebuffer`를 상속하는 kext로 만든다. `IOGraphicsFamily`(599)는 현재 게스트에 로드되어 있다.
2. **전송 계층은 직접 구현한다 (Q-07 확정).** `AppleVirtIOTransport`는 비공개 API라 헤더가 없고 macOS 업데이트에 따라 바뀔 수 있다. 공개 API인 `IOPCIDevice`만 쓴다.
3. **로드 방식**: SIP가 켜져 있으므로 서명 없는 kext를 `kmutil load`로 올릴 수 없다. OpenCore 주입은 SIP를 켠 채로 쓸 수 있다. **개발과 설치 모두 OpenCore `Kernel > Add` 주입만 쓴다 (Q-01 확정).** 시험할 때마다 재부팅이 필요하다. SIP는 바꾸지 않는다.
4. **빌드 도구**: Xcode 26.3에 kext 프로젝트 템플릿이 남아 있는지 확인하지 못했다. Phase 0에서 확인하고, 없으면 `xcrun clang -mkernel` 기반 Makefile을 쓴다. 대상 SDK는 설치된 `MacOSX26.2.sdk`, 배포 대상(`MACOSX_DEPLOYMENT_TARGET`)은 15.0.
5. **디버깅**: QEMU gdbstub(`-s`)와 빌드 24H241용 KDK로 소스 수준 디버깅을 한다. KDK는 Apple Developer 사이트에서 사용자가 받아 설치한다 (sudo 필요).
6. **화면 갱신 방식**: virtio-vga는 virtio-gpu 드라이버가 `SET_SCANOUT`을 하면 VGA(BAR0) 출력을 멈춘다. 따라서 BAR0을 계속 쓰는 방식은 해상도 제약(§2.6)을 풀지 못하며, 게스트 RAM backing + 주기 flush 방식을 쓴다.

## 8. 호스트(QEMU) 쪽 요구사항

호스트 설정은 이 프로젝트가 수정하지 않는다. 다음 조건을 사용자가 맞춘다.

### 8.1 현재 호스트 설정 (2026-11-23 사용자 제공)

| 항목          | 값                                                                                  |
| ------------- | ----------------------------------------------------------------------------------- |
| QEMU          | 10.0.13                                                                             |
| `-device`     | `qemu-xhci,id=xhci`, `usb-kbd,bus=xhci.0`, `usb-tablet,bus-xhci.0`(아래 참고), `usb-ehci,id=ehci`, `isa-applesmc`, `ich9-intel-hda`, `ich9-ahci`, `virtio-vga` |
| 디스플레이 장치 | `virtio-vga` 속성 지정 없음 → 아래 기본값을 그대로 쓴다 |
| 디스플레이 백엔드 | GTK (`-display gtk`). 사용자 보고: `-display gtk,gl=on`으로 바꾸면 `virtio-vga-gl`을 쓸 수 있다 |

해석:

- 속성을 지정하지 않았으므로 virtio-gpu 기본값 `xres=1280`, `yres=800`이 적용된다. **게스트의 1280×800 고정 해상도(§2.3)는 이 기본값 때문이다** (사용자가 준 `help` 출력으로 확인).

#### 8.1.1 `virtio-vga` 기본값 중 드라이버 설계와 관련된 것 (QEMU 10.0.13, `qemu-system-x86_64 -device virtio-vga,help`)

| 속성             | 기본값          | 설계 영향                                                                                   |
| ---------------- | --------------- | ------------------------------------------------------------------------------------------- |
| `xres` / `yres`  | 1280 / 800      | 장치가 알려 주는 선호 해상도. REQ-041의 기본 모드가 된다                                    |
| `edid`           | on              | `VIRTIO_GPU_F_EDID`가 제공된다. REQ-025를 바로 시험할 수 있다                               |
| `max_outputs`    | 1               | scanout 1개. 다중 모니터(REQ-046)는 호스트 설정 변경 없이는 시험할 수 없다                  |
| `max_hostmem`    | 268435456 (256 MiB) | 호스트 쪽 resource 메모리 상한으로 보인다(확인 필요). 128 MiB 풀(REQ-036)과 2560×1600 버퍼(16 MiB)는 이 안에 들어간다 |
| `blob`           | off             | blob resource 없음. REQ-027(협상 안 함)과 맞다                                              |
| `disable-legacy` | auto            | PCIe 슬롯이라 legacy 인터페이스가 꺼지는 것으로 보인다. modern 전송만 구현한다(REQ-010~012) |
| `disable-modern` | off             | modern 인터페이스 사용 가능                                                                  |
| `indirect_desc`  | on              | `VIRTIO_F_RING_INDIRECT_DESC` 제공 (REQ-016)                                                |
| `event_idx`      | on              | `VIRTIO_F_RING_EVENT_IDX` 제공 (REQ-016)                                                    |
| `packed`         | off             | packed virtqueue 없음. split virtqueue만 구현한다 (REQ-013)                                  |
| `vectors`        | 3               | MSI-X 벡터 3개 — config 변경 1 + controlq 1 + cursorq 1로 배정할 수 있다 (REQ-014)          |
| `page-per-vq`    | off             | notify 영역에서 큐마다 `queue_notify_off × notify_off_multiplier`로 주소를 계산한다          |
| `queue_reset`    | on              | `VIRTIO_F_RING_RESET` 제공. 이번 범위에서는 쓰지 않는다                                     |
| `ioeventfd`      | off             | 호스트 성능 관련. 게스트 드라이버 동작에는 영향 없음                                         |

#### 8.1.2 `virtio-vga-gl` (`-display gtk,gl=on`)에 대한 판단

- `virtio-vga-gl`은 호스트 쪽에 virgl 3D 렌더러를 켠 장치다. 게스트가 3D를 쓰려면 게스트 안에 **virgl 명령을 만들어 내는 3D 드라이버**가 있어야 한다. Linux는 Mesa(virgl Gallium 드라이버)가 그 역할을 하지만, macOS에는 해당 드라이버가 없다. macOS 앱은 Metal을 쓰므로, Metal 드라이버(비공개 `IOGPUFamily`·Metal 드라이버 번들 인터페이스)를 새로 만들어 virgl/Venus 명령으로 번역해야 한다. 이 작업의 가능성과 규모는 확인하지 못했고, 이번 2D 드라이버의 범위를 크게 넘는다.
- 따라서 **`virtio-vga-gl`로 바꿔도 이 드라이버만으로는 게스트에 Metal 장치가 생기지 않고 Sequoia 배경 문제(§2.5.2)도 그대로다.**
- 호스트 `gl=on`은 호스트 화면 출력에 GPU를 쓰므로 호스트 쪽 표시 성능에는 도움이 될 수 있다(측정하지 않음).
- 1차 개발·시험 장치는 현재 쓰고 있는 `virtio-vga`로 하고, `virtio-vga-gl`은 호환성 시험 대상(REQ-007)으로 둔다 (Q-13 확정).
- 현재 `virtio-vga`는 3D가 없는 장치이므로 HST-001을 만족한다.
- GTK 백엔드는 창 크기가 바뀌면 QEMU에 새 표시 크기를 알린다. 이것이 게스트에 `VIRTIO_GPU_EVENT_DISPLAY`로 전달되는지는 Phase 1~3에서 확인한다 (REQ-026, REQ-043).
- 제공된 목록의 `usb-tablet,bus-xhci.0`은 `bus=xhci.0`의 오타로 보인다. 옮겨 적을 때 생긴 오타일 수 있으며 이 프로젝트와는 관계없다.
- 드라이버 개발에 당장 필요한 호스트 변경은 없다. HST-002(선호 해상도), HST-005(gdbstub)는 해당 Phase에서 필요할 때 사용자가 추가한다.


#### 8.1.3 전원·잠금 동작 (2026-11-23 사용자 답변과 `pmset -g`, `sysadminctl -screenLock status`)

| 항목                 | 값                                                                       |
| -------------------- | ------------------------------------------------------------------------ |
| 시스템 잠자기        | 사용자 답변: 사용하지 않음. 단 `pmset`의 `sleep` 값은 1(분)이다 (R-09)    |
| 디스플레이 잠자기    | `displaysleep 10` (10분)                                                 |
| 화면 잠금            | `screenLock delay is immediate` — 디스플레이가 꺼지면 바로 잠긴다         |
| 사용자 보고          | 일정 시간 후 로그인 화면으로 전환된다                                    |

해석: "일정 시간 후 로그인 화면"은 디스플레이 잠자기(10분) 뒤 즉시 잠금이 걸리는 동작으로 보인다. 따라서 드라이버는 시스템 잠자기(REQ-054, 제외)가 아니라 **디스플레이 잠자기·깨우기와 잠금 화면 전환(REQ-053, AC-09)**을 반드시 처리해야 한다.
### 8.2 호스트 조건

| ID      | 요구사항                                                                                          |
| ------- | ------------------------------------------------------------------------------------------------- |
| HST-001 | 1차 시험 장치는 `-device virtio-vga`. `virtio-vga-gl`(`-display gtk,gl=on` 필요)은 REQ-007 호환 시험에만 쓴다 |
| HST-002 | `edid`는 기본값 on (확인). 선호 해상도를 바꾸려면 `xres=`, `yres=`를 지정한다. 예: `-device virtio-vga,xres=1920,yres=1080`. 드라이버가 완성되기 전에는 이 설정만으로도 GOP 해상도가 바뀌는지 시험해 볼 수 있다(§2.6, BAR0 8 MiB 한도) |
| HST-003 | 다중 모니터를 시험할 때만 `max_outputs=N`                                                         |
| HST-004 | 동적 해상도(REQ-043)는 창 크기 변경 이벤트를 보내는 디스플레이 백엔드에서만 시험할 수 있다. 현재 GTK 백엔드는 이 조건에 해당하는 것으로 보며, 실제 이벤트 전달은 Phase 3에서 확인한다 |
| HST-005 | 디버깅용으로 gdbstub(`-s` 또는 `-gdb tcp::1234`)를 켤 수 있어야 한다                              |
| HST-006 | 드라이버 설치 전 게스트 디스크 스냅샷을 만든다 (qcow2 `snapshot` 또는 파일 복사)                  |

## 9. 검증과 인수 기준

### 9.1 인수 기준

| ID     | 기준                                                                                             | 관련 요구사항     |
| ------ | ------------------------------------------------------------------------------------------------ | ----------------- |
| AC-01  | `ioreg -c IOFramebuffer`에서 이 드라이버 클래스가 디스플레이 장치에 붙어 있고 `IONDRVFramebuffer`는 없다 | REQ-001, 002 |
| AC-02  | `system_profiler SPDisplaysDataType`의 VRAM이 REQ-060 값으로 표시된다                            | REQ-060, 061      |
| AC-03  | 시스템 설정 > 디스플레이에 REQ-040 모드 목록이 보이고, 각 모드로 바꾸면 실제 해상도가 바뀐다(스크린샷 크기로 확인) | REQ-040, 042 |
| AC-04  | 색상 확인 패턴(빨강·초록·파랑·흰색) 화면에서 색이 뒤바뀌지 않는다                               | REQ-022           |
| AC-05  | boot-arg로 드라이버를 끄면 기존 1280×800 부트 프레임버퍼로 부팅된다                               | REQ-072, G-4      |
| AC-06  | 장치 초기화를 강제로 실패시켜도(디버그 옵션) 부팅이 계속되고 `IONDRVFramebuffer`가 남는다         | REQ-004, 005      |
| AC-07  | 드라이버 적용 전/후로 비교해 기록한다. ① 정적 사진 배경(Sonoma.heic) 정상 표시 유지 ② Sequoia 배경 표시 여부(해결 기대 없음, 기록만) ③ 단색 배경의 16 px 테두리 ④ 메뉴 막대·Dock 투명 효과 ⑤ `MTLCopyAllDevices()` 결과 | G-5 |
| AC-08  | NFR-001, NFR-002 시험을 통과하고 결과를 기록한다                                                 | NFR-001, 002      |
| AC-09  | 디스플레이 잠자기 → 깨우기 → 잠금 화면 → 로그인을 10회 반복해 화면 깨짐·검은 화면·패닉이 없다. 각 회차 해상도가 유지된다 | REQ-053, NFR-001 |

### 9.2 기록

- 적용 전/후 `scripts/collect-env.sh` 출력을 `docs/research/`에 날짜별로 남긴다.
- 화면 확인 결과는 `docs/screenshots/`, 동작 녹화는 `docs/movie/`에 남긴다. 녹화에는 지금 하는 동작을 설명하는 자막을 넣는다.
- 이 프로젝트는 웹 UI가 없어 Playwright E2E 대상이 아니다. 대신 시스템 설정 > 디스플레이 조작을 `screencapture -v`로 녹화하고 자막을 넣는다 (Q-09 확정). 녹화 간격은 전역 규칙을 따른다: 화면 전환 사이 1.5초, 같은 화면의 값 입력 사이 0.3~0.5초.

## 10. 가정, 제약, 리스크

### 10.1 가정

- A-1: 게스트는 OpenCore로 부팅하며 사용자가 EFI 파티션의 `config.plist`와 `Kexts/`를 고칠 수 있다.
- A-2: 호스트 QEMU는 10.0.13이고 `edid` 기본값이 on임을 확인했다. `VIRTIO_GPU_EVENT_DISPLAY` 지원은 `help` 출력에 나오지 않아 확인하지 못했다. Phase 1에서 게스트가 읽은 feature 비트와 config 값으로 확정한다.
- A-3: 개발과 시험은 이 VM 안에서 하고, 문제가 생기면 호스트에서 디스크 스냅샷으로 복구할 수 있다.

### 10.2 리스크

| ID   | 리스크                                                                                 | 영향 | 대응                                                                 |
| ---- | -------------------------------------------------------------------------------------- | ---- | -------------------------------------------------------------------- |
| R-01 | kext 결함으로 부팅 중 패닉 → VM 부팅 불가                                              | 높음 | REQ-072 boot-arg, OC 부팅 항목 분리, HST-006 스냅샷                  |
| R-02 | `AppleVirtIOPCITransport`가 같은 장치를 이미 초기화·점유하고 있어 충돌                 | 높음 | Phase 1 첫 작업으로 확인 (REQ-003)                                   |
| R-03 | Sequoia 등 확장형 배경과 투명·블러 효과는 Metal이 필요해 2D 드라이버로 해결되지 않음 (§2.5.2로 Metal 부재 확인) | 높음 | 사용자에게 범위 밖임을 알림. 정적 이미지 배경으로 우회. Metal 지원은 범위 밖, 2D 완료 후 별도 조사(Q-12 확정) |
| R-04 | 주기 flush로 CPU 사용률이 높거나 화면 찢김 발생                                        | 중간 | REQ-033 변경 감지, 주기 조정, NFR-004 측정                           |
| R-05 | macOS 업데이트로 `IOGraphicsFamily` 동작 변경 또는 지원 축소                           | 중간 | 대상 빌드를 고정해 시험, 업데이트 전 스냅샷                          |
| R-06 | WindowServer가 부트 프레임버퍼에서 새 드라이버로 넘어가지 않음 (probe 순서·카테고리 문제) | 높음 | Phase 2 초기에 최소 드라이버로 대체 여부부터 확인 (REQ-002)          |
| R-07 | 빌드 24H241에 맞는 KDK를 구할 수 없음                                                  | 낮음 | 가까운 빌드 KDK 사용 또는 로그 기반 디버깅                            |
| R-08 | `system_profiler` VRAM 표시가 `IOFBMemorySize` 외 비공개 속성에 의존                   | 낮음 | REQ-061 조사, 안 되면 `IOFBMemorySize` 기준으로 인수 기준 조정(승인 필요) |
| R-09 | `pmset`의 시스템 잠자기 값이 `sleep 1`(1분)로 설정돼 있다. 지금은 다른 프로세스의 잠자기 방지 요청 때문에 실제로 잠들지 않았을 수 있다. 사용자는 잠자기를 쓰지 않는다고 했으므로, 실제로 잠들면 REQ-054 제외 범위에 걸린다 | 중간 | 사용자가 `sudo pmset -a sleep 0`으로 잠자기를 끄는 것을 권장(사용자 직접 실행). 드라이버는 잠자기 요청에 패닉 없이 응답만 한다 |

## 11. Phase 후보

| Phase | 이름                     | 주요 내용                                                                 | 관련 REQ                  |
| ----- | ------------------------ | ------------------------------------------------------------------------- | ------------------------- |
| P0    | 개발 환경                | 빌드 체계 확정, 빈 kext 빌드·로드, OC 주입·롤백 절차, KDK·gdbstub 디버깅 | REQ-006, 070~072, 075     |
| P1    | virtio 전송 계층         | AppleVirtIO 충돌 확인, capability 파싱, 초기화, virtqueue, 인터럽트       | REQ-003, 010~018          |
| P2    | 2D 단일 모드 출력        | display info, resource·backing·scanout·flush, IOFramebuffer 고정 모드, 대체 확인 | REQ-001, 002, 004, 005, 007, 020~024, 027, 030~032, 034, 035 |
| P3    | 모드와 VRAM              | 모드 목록, 모드 전환, EDID, 디스플레이 이벤트, VRAM 보고                  | REQ-025, 026, 036, 040~045, 060~064 |
| P4    | 커서·전원·최적화         | 하드웨어 커서, 감마, 잠자기, 변경 감지 flush                              | REQ-033, 050~054          |
| P5    | 안정화와 문서            | NFR 시험, 배경화면 비교(AC-07), 설치 문서 정리, 도구                      | REQ-074, NFR 전체         |
| P6    | 확장(선택)               | 다중 모니터, 동적 리사이즈 자동 전환                                      | REQ-043, 046              |

## 12. Open Questions

답변 형식 예: `Q-01: A`, `Q-03: 권장안`. 2026-11-23 사용자 지시("Use the recommendations")로 선택형 질문은 권장안으로 확정했다. **상태** 열이 "확정"인 항목은 결정이 끝난 것이다. Q-04·Q-08은 선택이 아니라 정보 요청이라 열려 있다.

| ID   | 상태 | 질문                                                                                     | 선택지                                                                                         | 권장안 |
| ---- | ---- | ---------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- | ------ |
| Q-01 | 확정 | 개발 중 kext를 어떻게 올릴까?                                                            | A) OpenCore 주입만(SIP 유지, 매번 재부팅) B) SIP 부분 해제 + `kmutil load`(빠름, 보안 약화) C) A를 기본으로, 필요할 때만 B | A. 재부팅이 필요하지만 SIP를 유지하고 실패 시 OC에서 바로 끌 수 있다. Lilu/WhateverGreen이 OC로 주입되는지도 함께 확인 필요 |
| Q-02 | 확정 | 지원할 최대 해상도는?                                                                    | A) 1920×1200 B) 2560×1600 C) 3840×2160                                                         | B. 버퍼 하나에 약 16 MiB로 메모리 부담이 작고 HiDPI 1280×800을 지원할 수 있다 |
| Q-03 | 확정 | 프레임버퍼 메모리 풀(= 보고할 VRAM) 기본 크기는?                                         | A) 현재 모드 크기만 B) 64 MiB C) 128 MiB D) 256 MiB                                             | C. 최대 모드 버퍼 2개 + 여유. 메모리 32 GiB 중 wired 128 MiB는 부담이 작다 |
| Q-04 | 확정 | 호스트 실행 정보: OSX-KVM 실행 스크립트의 장치 옵션, QEMU 버전, 디스플레이 백엔드(GTK/SPICE/VNC), VM 잠자기 사용 여부 | 실행 스크립트 내용 공유 | 받음: QEMU 10.0.13, `-device` 목록(§8.1), `virtio-vga` 기본값(§8.1.1), 백엔드 GTK(§8.1.2), 잠자기 사용 안 함·일정 시간 후 로그인 화면(§8.1.3) |
| Q-05 | 확정 | macOS 14 이하도 지원할까?                                                                | A) 15만 B) 14·15 C) 13 이상                                                                     | A. 현재 VM이 15이고 시험 환경이 하나다. 필요해지면 그때 확장 |
| Q-06 | 확정 | 배포 범위는?                                                                             | A) 개인 사용 B) GitHub 공개(빌드 결과물, 서명 없음, OC 주입 전제) C) Apple kext 서명 포함 공개  | A로 시작, 안정화 후 B 검토. C는 Apple의 kext 서명 인증서가 필요하다 |
| Q-07 | 확정 | virtio 전송 계층을 직접 구현할까, Apple `AppleVirtIOTransport`에 붙을까?                 | A) 직접 구현(공개 API만) B) Apple 비공개 API 사용                                               | A. 비공개 API는 헤더가 없고 업데이트에 따라 깨질 수 있다 |
| Q-08 | 답변 | "배경화면 등이 제대로 표현이 안 된다"는 구체적으로 어떤 모습인가? | 검은 배경 / 바뀌지 않음 / 흐림·투명 효과 없음 / 깨짐 등 | 사용자 캡처와 보고로 답변. 사진으로 설정했는데 단색으로 표시됨, 단색 표시 시 16 px 테두리, 메뉴 막대·Dock 불투명 (§2.5) |
| Q-09 | 확정 | 검증 기록 방식                                                                          | A) 시스템 설정 조작을 `screencapture -v`로 녹화 + 자막 B) 스크린샷만                            | A. 전역 규칙(스크린샷·동영상·자막)을 드라이버 프로젝트에 맞게 적용 |
| Q-10 | 확정 | 라이선스                                                                                 | A) MIT B) BSD-2-Clause C) Apache-2.0 D) 미정(비공개)                                            | A(MIT)로 확정. 권장안이 "A 또는 B"였으므로 선택지 순서상 앞인 A를 택했다. GPL 코드를 쓰지 않으므로 허용적 라이선스를 쓸 수 있다 |
| Q-11 | 답변 | 사진 배경에서도 같은 현상인가? | A) 사진 배경으로 잠시 바꿔 캡처 B) 단색 기준만 사용 | A로 시험 완료. 정적 사진(Sonoma.heic)은 정상, 사용자가 고른 배경은 Sequoia(Metal 확장형)로 Cyan 대체 (§2.5.2) |
| Q-12 | 확정 | Sequoia 배경·투명 효과를 위해 Metal 가속을 목표에 넣을까? | A) 이번 범위 밖 유지(2D 드라이버 먼저) B) 2D 완료 후 별도 프로젝트로 가능성 조사 C) 호스트 GPU 패스스루 등 드라이버 밖의 방법 검토 D) `virtio-vga-gl` + virgl 위에 Metal 드라이버 개발 | **확정: A + B.** D는 macOS용 virgl 3D 드라이버가 없어 Metal 드라이버를 새로 만들어야 하고(§8.1.2), 비공개 인터페이스라 가능성 자체가 불확실하다 |
| Q-13 | 확정 | 1차 개발·시험 장치를 무엇으로 할까? | A) `virtio-vga` 1차, `virtio-vga-gl`은 호환 시험(REQ-007) B) `virtio-vga-gl` 1차 C) `virtio-vga`만 | **확정: A.** 지금 VM 설정 그대로 개발할 수 있고, 2D 드라이버는 gl 변형의 3D 기능을 쓰지 않는다 |

## 13. 참고 자료

| 자료                                                         | 용도                                     |
| ------------------------------------------------------------ | ---------------------------------------- |
| OASIS *Virtual I/O Device (VIRTIO) Version 1.2/1.3* — §4.1 PCI 전송, §2.7 split virtqueue, §5.7 GPU Device | 전송 계층과 GPU 명령의 기준 사양 |
| Apple `Kernel.framework` 헤더: `IOFramebuffer.h`, `IOGraphicsTypes.h`, `IOPCIDevice.h` (설치된 SDK) | IOKit API 기준 |
| Apple IOGraphics 오픈소스 (apple-oss-distributions/IOGraphics) | `IONDRVFramebuffer` 동작, 모드 처리 참고 |
| QEMU `hw/display/virtio-gpu*.c`, `virtio-vga.c` (GPL — 동작 이해용, 복사 금지) | 호스트 쪽 동작·BAR 배치 확인 |
| OpenCore Configuration.pdf — `Kernel > Add`, `UEFI > Output`  | 설치·롤백 절차                           |
