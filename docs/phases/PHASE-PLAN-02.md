# PHASE-PLAN-02 — 2D 단일 모드 출력

| 항목      | 내용                                                                 |
| --------- | -------------------------------------------------------------------- |
| Phase     | P2                                                                   |
| 기간      | 12.5 Man-day                                                         |
| 관련 요구 | REQ-001, 002, 004, 005, 007, 020~024, 030~032, 035, 036, 051, 052, NFR-006 |
| 선행      | P1 완료                                                              |
| 완료 조건 | **M1** — 이 드라이버가 부트 프레임버퍼를 대체해 1280×800 데스크톱을 virtio-gpu로 출력한다 (AC-01, AC-04, AC-05, AC-06) |

## 1. 목표

해상도는 1280×800 하나로 고정한 채, 화면 출력 경로 전체(메모리 → resource → scanout → flush)를 완성하고 `IONDRVFramebuffer`를 대체한다. 이 Phase가 프로젝트에서 가장 위험하다. 대체가 실제로 일어나는지(R-06)와 프레임버퍼 메모리 형태(T-01)를 **초기에** 확인한다.

## 2. 먼저 확인할 기술 사항

### T-01 프레임버퍼 메모리의 물리 연속성

- `IOFramebuffer::getApertureRange(IOPixelAperture)`는 `IODeviceMemory *`를 돌려준다. `IODeviceMemory`는 물리 주소 범위로 만든다. 따라서 WindowServer가 매핑할 프레임버퍼는 **물리적으로 연속**이어야 할 가능성이 높다.
- REQUIREMENT REQ-030은 "물리 연속일 필요는 없다"고 되어 있다. virtio-gpu 쪽(`ATTACH_BACKING`)은 연속이 아니어도 되지만, IOFramebuffer 쪽 제약 때문에 결국 연속 할당이 필요할 수 있다.
- 확인 방법(WI-2.003): `IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task, kIOMemoryPhysicallyContiguous | …)`로 2560×1600×4(약 16 MiB) 연속 할당이 부팅 직후 성공하는지 시험한다.
- 연속 할당이 필요하면 REQ-030 문구 변경을 사용자에게 제안한다 (요구사항은 승인 없이 바꾸지 않는다).

### R-06 부트 프레임버퍼 대체

- 현재 `IONDRVFramebuffer`: `IONameMatch "display"`, `IOProbeScore 20000`, `IOMatchCategory IOFramebuffer`.
- WI-2.006에서 최소 IOFramebuffer(단색 화면만)로 대체가 되는지 먼저 확인한 뒤 나머지를 붙인다. 실행 순서를 그렇게 배치했다 (§3).

## 3. 의존 관계와 실행 순서

```
WI-2.001 ─> WI-2.002 ─┐
WI-2.003 ─> WI-2.004 ─┼─> WI-2.005(최소) ─> WI-2.006 ─> WI-2.007 ─> WI-2.008 ─> WI-2.009 ─> WI-2.010
```

1. WI-2.001, WI-2.002 / WI-2.003(T-01 시험 포함), WI-2.004
2. WI-2.005를 **최소 형태**(모드 1개, flush 없이 단색)로 만들고 바로 WI-2.006 대체 시험
3. 대체가 확인되면 WI-2.005 나머지, WI-2.007 flush
4. WI-2.008 실패 경로, WI-2.009 부트 화면, WI-2.010 gl 호환

## 4. Work Item 상세

### WI-2.001 GPU 명령 계층 (1.5)

```cpp
// 스케치
IOReturn VirtioGpuDevice::submit(const VirtioGpuCtrlHdr &cmd, size_t cmdLen,
                                 VirtioGpuCtrlHdr *resp, size_t respLen);
```

- `IOCommandGate`로 직렬화, 응답 대기는 `commandSleep` + 타임아웃 상수 `kCommandTimeoutMs`.
- 응답 `type`이 `RESP_OK_*`가 아니면 명령 이름·주요 인자 로그 후 `kIOReturnError` 계열 반환.
- 응답 길이가 기대보다 짧으면 거절 (NFR-006).

### WI-2.002 display info 조회 (0.5)

- `num_scanouts`(device config) ≤ 16 검사, scanout 0이 enabled인지 확인.
- 선호 크기가 0이거나 상한(2560×1600) 초과면 기본 모드(1280×800)로 대체하고 로그.

### WI-2.003 프레임버퍼 메모리 풀과 backing (1.5)

- 풀 상한 128 MiB (REQ-036). 이 Phase에서는 현재 모드 버퍼 1개만 할당.
- T-01 시험 결과로 할당 방식을 정하고 §6에 기록.
- `RESOURCE_ATTACH_BACKING`: `IODMACommand`로 세그먼트 목록을 얻어 `virtio_gpu_mem_entry` 배열로 보낸다. 연속 할당이면 항목 1개.

### WI-2.004 2D resource 생명주기와 픽셀 형식 (1.5)

- 형식: macOS 32bpp 화면의 메모리 바이트 순서는 B,G,R,A → `VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM`(**2**) 우선. 색이 틀리면 `B8G8R8A8`(1) 등으로 바꿔 시험한다. (초안의 값 1/2가 뒤바뀌어 있던 것을 WI-0.008에서 사양으로 확인해 고쳤다. 상수는 `kext/include/vv/VirtioGpuSpec.hpp`)
- resource id는 1부터 증가, 0은 쓰지 않는다 (scanout 해제용).
- 수락 기준: 시험 패턴(빨강·초록·파랑·흰색 띠)을 커널에서 채워 출력하고 스크린샷으로 색 확인 (AC-04).

### WI-2.005 IOFramebuffer 하위 클래스 (2.5)

구현할 메서드 (SDK `IOFramebuffer.h` 기준):

| 메서드                         | 이 Phase의 동작                                      |
| ------------------------------ | ---------------------------------------------------- |
| `enableController`             | 장치 초기화 완료 확인, 첫 resource·scanout 설정      |
| `getApertureRange`             | 현재 프레임버퍼 메모리 범위                          |
| `getVRAMRange`                 | 풀 범위 (P3에서 VRAM 보고와 맞춤)                    |
| `getPixelFormats`              | 32bpp `IO32BitDirectPixels`                          |
| `getDisplayModeCount` / `getDisplayModes` | 1개 (1280×800)                            |
| `getInformationForDisplayMode` | 1280×800, 60 Hz, 깊이 1개                            |
| `getPixelFormatsForDisplayMode`| 0                                                    |
| `getPixelInformation`          | rowBytes = width×4, 32bpp, 마스크                    |
| `getCurrentDisplayMode` / `getStartupDisplayMode` | 고정 모드                         |
| `setDisplayMode`               | 같은 모드만 허용 (P3에서 확장)                       |
| `setGammaTable`                | 값 저장 후 성공 (REQ-052)                            |
| `getAttribute(kIOHardwareCursorAttribute)` | 미지원 보고 → 소프트웨어 커서 (REQ-051)  |
| `isConsoleDevice`              | true (부트 콘솔 이어받기)                            |

### WI-2.006 부트 프레임버퍼 대체 (1.5)

personality 초안:

```xml
<key>IOProviderClass</key><string>IOPCIDevice</string>
<key>IOPCIMatch</key><string>0x10501af4</string>
<key>IOMatchCategory</key><string>IOFramebuffer</string>
<key>IOProbeScore</key><integer>(20000보다 큰 값 — 시험으로 확정)</integer>
<key>IOClass</key><string>(프레임버퍼 클래스 이름)</string>
```

- 수락 기준: `ioreg -c IOFramebuffer`에 이 클래스만 있고 `IONDRVFramebuffer` 없음 (AC-01), `system_profiler`가 이 드라이버를 표시.
- 대체되지 않으면: 매칭 시점, `IONDRVFramebuffer`의 종료 조건을 IOGraphics 오픈소스로 조사해 원인과 대안을 기록하고 사용자와 방향을 정한다.

### WI-2.007 주기 flush 타이머 (1)

- `IOTimerEventSource`, 기본 주기 16 ms(60 Hz)를 상수·설정값으로.
- 매 주기 `TRANSFER_TO_HOST_2D`(전체 화면) + `RESOURCE_FLUSH`. 이전 flush가 끝나지 않았으면 이번 주기를 건너뛴다.
- 수락 기준: 창 이동·타이핑이 반영되는 녹화, flush 횟수 통계.

### WI-2.008 실패 경로와 자원 해제 (1)

- 디버그 boot-arg `vvideo_fail=<단계>`로 capability/feature/queue/resource/scanout 단계 실패를 주입.
- 각 실패에서 할당 역순 해제, 장치 reset, `start()` false → `IONDRVFramebuffer` 유지 (AC-06).
- boot-arg `-vvideooff` 부팅 확인 (AC-05).

### WI-2.009 부트 화면 이어받기 (1)

- 드라이버가 scanout을 설정하기 직전 BAR0의 현재 화면(1280×800)을 새 프레임버퍼로 복사한다.
- 수락 기준: 부팅 녹화에서 진행 막대가 끊기지 않거나 깜박임 1회 이내.

### WI-2.010 virtio-vga-gl 호환 시험 (0.5)

- 사용자가 호스트를 `-device virtio-vga-gl -display gtk,gl=on`으로 바꿔 부팅.
- 확인: `VIRGL` feature가 제공되지만 협상하지 않음, 2D 출력 정상 여부. 결과만 기록하고 원래 설정으로 되돌린다 (Q-13).

## 5. 검증 흐름과 기록

- AI: 코드·단위 테스트·빌드, 로그/ioreg 판정, 스크린샷 픽셀 측정.
- 사용자: 설치·재부팅, 호스트 설정 변경(WI-2.010).
- 기록: `docs/screenshots/p2/`(색 패턴, 데스크톱), `docs/movie/p2-boot.mov`(부팅), `docs/research/`(수집 결과).

## 6. 결정 기록

### WI-2.001 GPU 명령 계층 (2026-11-24)

구현 (kext 0.0.17)

- 명령 계층을 `kext/src/VirtioGpuDeviceCommands.cpp`로 분리했다 (클래스는 `VirtioGpuDevice` 그대로). 두 구현 파일이 같이 쓰는 상수는 `VirtioGpuDevicePrivate.hpp`.
- `vv/GpuCommand.hpp`: 사양의 모든 명령·응답 이름(`ctrlTypeName`), 응답 검사 결과 + `RESP_ERR_*`를 합친 `CommandResult`. 테스트 2개 추가.
- `sendCommand(request, response, expected, logArg)`: 직렬 제출(WI-1.007의 `submitCommand`) → 길이·type 검사 → `CommandResult` → IOReturn. 오류 응답은 `kIOReturnBadArgument`(잘못된 id·인자), `kIOReturnNoMemory`(메모리 부족), `kIOReturnIOError`(그 밖) 로 바꾼다. 실패하면 명령 이름·주요 인자·응답 이름·장치가 쓴 길이를 로그로 남긴다. 워크루프 스레드에서 부르면 교착되므로 `kIOReturnNotPermitted`로 거절한다.
- `sendNodataCommand`: 응답이 헤더뿐인 명령용. P2 이후 resource·scanout·flush 명령은 이것을 쓴다.
- 디버그 빌드 자체 시험: 시작 때 없는 resource(`0xfffffff0`) `RESOURCE_UNREF` → `ERR_INVALID_RESOURCE_ID` 기대.

실기 확인

| 항목 | 결과 |
| ---- | ---- |
| GET_DISPLAY_INFO (sendCommand 경유) | `0:1280x800` |
| 자체 시험 | `invalid-unref=err-invalid-resource-id ioreturn=0xe00002c2 pass` |
| 로그 | `RESOURCE_UNREF arg=0xfffffff0 -> ERR_INVALID_RESOURCE_ID (err-invalid-resource-id, written 24)` |
| 카운터 | `commands=2 errors=1 timeouts=0 recoveries=0` (오류 1은 자체 시험) |

확인하지 못한 것: 시간 초과 경로(장치가 항상 응답). WI-1.006에서 같은 복구 경로를 외부 reset 흉내로 확인했다.

### WI-2.002 display info 조회 (2026-11-24)

- `vv/DisplayMode.hpp`의 `preferredModeFrom`: scanout이 꺼져 있거나, 크기가 0이거나, 2560×1600(Q-02)을 넘으면 1280×800으로 대체. 장치 config의 `num_scanouts`는 응답 배열 크기(16)로 자른다 (WI-1.007의 `usableScanouts`).
- 실기 (kext 0.0.18): `VVPreferredMode = 1280x800` (장치 값 사용). 단위 테스트로 0·꺼짐·초과 대체를 확인.

### WI-2.003 프레임버퍼 메모리 (2026-11-24) — T-01 해결

- `vv/DisplayMode.hpp`: 32bpp, 한 줄 = width×4, 버퍼 크기는 page 단위 올림, 풀 상한 128 MiB(Q-03). 최대 모드 버퍼 2개(약 31 MiB)도 풀 안에 들어간다.
- `vv/MemEntries.hpp`: DMA 세그먼트를 `virtio_gpu_mem_entry` 목록으로. 이어지는 세그먼트는 합치고, 길이 0·32비트 초과·칸 부족은 실패. 전송은 WI-2.004.
- `VVDmaBuffer::physicalAddress()`: `getPhysicalSegment(kIOMemoryMapperNone)`로 CPU 물리 주소를 얻고, 버퍼 전체가 한 덩어리일 때만 값을 둔다.
- 시작 때 선호 모드 크기의 프레임버퍼를 할당해 둔다 (`VVFramebuffer`).

**T-01 판정 (실기, kext 0.0.18)**

| 확인 | 결과 |
| ---- | ---- |
| 1280×800 프레임버퍼 | 4,096,000바이트, 물리 연속, 장치 주소 = 물리 주소 |
| 2560×1600 연속 할당 시험 (`VVContigProbe`) | 16,384,000바이트 `ok-contiguous` |

결정: 프레임버퍼는 **물리 연속으로 할당**하고, `getApertureRange()`는 그 물리 주소 범위로 `IODeviceMemory`를 만든다. 장치 주소와 물리 주소가 같으므로(IOMMU 없음) `ATTACH_BACKING`에는 항목 1개(장치 주소, 전체 길이)를 보낸다. REQ-030은 "연속일 필요는 없다"는 허용 조건이라 연속 할당과 충돌하지 않으며, 요구사항은 바꾸지 않는다.

남은 위험: 부팅 직후가 아닌 실행 중 모드 전환(WI-3.002)에서는 메모리 단편화로 16 MiB 연속 할당이 실패할 수 있다. 그때는 시작 때 최대 모드 버퍼를 미리 잡아 두는 방식으로 바꾼다.

### WI-2.004 2D resource 생명주기와 픽셀 형식 (2026-11-24) — 첫 화면 출력

구현 (kext 0.0.19)

- resource 명령: `createResource2d`, `attachBacking`(물리 연속이라 항목 1개), `setScanout`(resource 0이면 끄기), `transferToHost`(전체, offset 0), `flushResource`, `detachBacking`, `unrefResource`. 모두 `sendNodataCommand`. resource id는 1부터 증가.
- `vv/TestPattern.hpp`: 세로 4등분 빨강·초록·파랑·흰색, 메모리 바이트 B,G,R,X. 테스트 3개.
- 시험 패턴 `VirtioGpuDevicePatternTest.cpp` (boot-arg `vvideo_test_pattern=1`): 시작 90초 뒤 패턴 표시 15초 → scanout 끄기 10초 → resource 정리 → 장치 reset·재초기화. 모든 명령은 thread_call에서 보내고, 재초기화는 게이트 안에서 한다.

실기 확인

| 단계 | 기록 | 사용자가 QEMU 창에서 본 것 |
| ---- | ---- | -------------------------- |
| 표시 (create·attach·transfer·set_scanout·flush) | `shown` | 왼쪽부터 **빨강·초록·파랑·흰색** — 색 순서 정상 (AC-04) |
| scanout 끄기 (resource 0) | `scanout-disabled` | **"Display output is not active"** |
| detach·unref | `released` | (같음) |
| 장치 reset·재초기화 | `reinitialized` | **macOS 화면(VGA)으로 정상 복귀** |

- 카운터: `commands=10 completions=10 timeouts=0 errors=1`(자체 시험), 상태 `0x0F`, 새 패닉 없음.
- **픽셀 형식 결정: `VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM`(2)** — macOS 32bpp 메모리 순서와 일치.
- 확인하지 못한 것: 호스트 캡처(사용자 육안 확인으로 대신함).

이후 설계에 쓰는 사실

- QEMU virtio-vga는 `SET_SCANOUT`으로 resource를 연결하는 순간 VGA 출력에서 virtio-gpu 출력으로 바뀐다. `GET_DISPLAY_INFO`·resource 생성만으로는 바뀌지 않는다 (WI-1.007, 이번 시험).
- scanout을 끄면(resource 0) VGA로 돌아가지 않고 "Display output is not active"가 된다.
- **장치를 reset하면 VGA 출력으로 돌아간다.** 그래서 WI-2.006 이후 드라이버가 화면을 넘겨받은 뒤 실패하면 장치를 reset해 펌웨어 화면(IONDRV/BAR0)으로 되돌리는 것을 안전장치로 쓴다 (REQ-005, AC-06).

### WI-2.005 1차 시도와 코드 유실 (2026-11-24)

자세한 경과는 [../WI-2.004.md](../WI-2.004.md) §4~§7 (Remote Control 세션 기록에서 복원).

- kext 0.0.20: `VirtioVideoFramebuffer`(IOFramebuffer, 1280×800, 물리 연속 aperture, `VVDmaBuffer` retain), `VirtioGpuDeviceScanout.cpp`(약 16ms 주기 전체 flush), takeover(`vvideo_takeover=1`, 시작 60초 뒤 `AAPL,iokit-ignore-ndrv` → IONDRV 종료 → 재매칭), personality `IOFramebuffer` 카테고리 점수 30000. 빌드 0, 테스트 63개 통과.
- boot-args `keepsyms=1 vvideo_takeover=1`로 첫 재부팅: **kext가 로드되지 않음**(로그에 이름조차 없음), 부팅·화면은 정상. 가설: IOFramebuffer 상속으로 IOGraphicsFamily 심볼이 필요해져 OC 주입 단계에서 버려짐 (T-04).
- 원인 확인용으로 OC `Misc > Debug > Target=65`, DisplayLevel INFO를 켜고 재부팅 → **Apple 로고에서 멈춤** → 사용자가 호스트 스냅샷 "WI-001"로 복구.
- 코드는 커밋 전이라 유실. 패닉·OC 로그도 사라짐. KDK도 지워짐(재설치 필요).
- 판단: 멈춘 부팅의 직전 변경은 OC 로그 설정뿐이었다. kext 0.0.20이 멈춤의 원인이라는 근거는 없다 (확인 못 함).

재시도 방침

- T-04를 먼저 가른다: 메서드 스텁만 있는 최소 IOFramebuffer 하위 클래스가 OC 주입으로 로드되는지 확인한다.
- OC 파일 로그는 켜지 않는다. 필요하면 `-v` 화면 로그나 호스트 직렬 로그(`-serial file:…`, boot-arg `serial=3`)를 쓰고, OC 설정 변경은 kext 변경과 다른 부팅에서 단독으로 한다.
- 시험 전 VM을 끄고 호스트 스냅샷, **설치 전 커밋·푸시**.

### T-04 재조사 (2026-11-24, 재부팅 없이)

| 확인 | 결과 |
| ---- | ---- |
| 부트 collection (`BootKernelExtensions.kc`) | IOPCIFamily 2.9, AppleVirtIO 230.100.3 있음. **IOGraphicsFamily 없음** |
| 시스템 collection (`SystemKernelExtensions.kc`) | **IOGraphicsFamily 599, IONDRVSupport 599** |
| 보조 collection (`/Library/KernelCollections/AuxiliaryKernelExtensions.kc`) | AppleMobileDevice, HighPointRR, HighPointIOP (서명된 서드파티 kext) |
| 디스크의 IOGraphicsFamily.kext | `Info.plist`, `iogdiagnose`만 있고 실행 파일 없음. KDK에도 실행 파일 없음(dSYM만) |

판단

- OpenCore는 주입 kext를 **부트 collection에 넣고 그 안의 심볼로 링크**한다. IOFramebuffer를 상속하면 IOGraphicsFamily 심볼(vtable, metaclass)이 필요하지만 부트 collection에는 없다. 0.0.20이 로그 한 줄 없이 사라진 것과 맞는다 (0.0.19까지는 IOGraphicsFamily 심볼을 쓰지 않아 늦게라도 로드됐다).
- 따라서 **OC 주입만으로는 IOFramebuffer 하위 클래스를 올릴 수 없을 가능성이 높다.** 아직 실기로 확정하지는 않았다.

해결 후보 (모두 결정 변경 또는 위험이 있어 사용자 승인 필요)

| 후보 | 내용 | 문제 |
| ---- | ---- | ---- |
| A. 보조 collection 설치 | 프레임버퍼 kext를 `/Library/Extensions`에 두어 보조 collection으로 로드. 보조 collection은 시스템 collection 심볼로 링크할 수 있다. 서드파티 디스플레이 kext의 정상 경로 | 서명되지 않은 kext라 SIP의 kext 서명 검사 해제(`csr-active-config`)와 시스템 설정 승인이 필요 → **Q-01(SIP 유지, OC 주입만) 변경** |
| A'. 둘로 나누기 | virtio 전송·GPU 장치는 지금처럼 OC 주입, 프레임버퍼만 보조 collection kext로 분리 | A와 같은 SIP 조건. 구조가 복잡해짐 |
| B. IOGraphicsFamily를 부트 collection에 함께 주입 | | 디스크·KDK 어디에도 실행 파일이 없다. 있어도 시스템 collection과 중복 로드로 부팅 불가 위험 |
| C. Lilu 방식 런타임 해석 | 링크 없이 실행 중에 IOGraphicsFamily 심볼을 찾아 쓰는 방식 | IOFramebuffer를 상속하려면 vtable을 직접 만들어야 해 매우 불안정. 비권장 |

### T-04 실기 확정 (2026-11-24, kext 0.0.21)

| 항목 | 값 |
| ---- | -- |
| 변경 | WI-2.004 재설치 상태(0.0.19)에서 **personality 없는 IOFramebuffer 스텁 클래스만 추가**. config.plist·boot-args(`keepsyms=1`)·OC 로그(Target 0)는 그대로 |
| kext | 0.0.21, UUID `95883E93-…`, IOFramebuffer 심볼 98개를 시스템 collection의 IOGraphicsFamily에서 가져옴 (`kmutil libraries -p`) |
| 결과 | 부팅·화면 정상. **`kmutil showloaded`에 kext 없음**, `ioclasscount`에 VirtioVideoProbe·VirtioGpuDevice·VirtioVideoFramebuffer 모두 "no such class", 통합 로그에 kext 이름 없음, 새 패닉 없음 |

결론: **IOGraphicsFamily 심볼을 쓰는 kext는 OpenCore 주입으로 로드되지 않는다 (T-04 확정).** 0.0.20이 로드되지 않은 원인도 이것이다. 실패할 때 kext 전체가 버려지므로 같은 kext 안의 virtio 전송·GPU 장치 서비스도 함께 사라진다.

다음 결정 필요: 해결 후보 A/A'(보조 collection, Q-01 변경). EFI에는 지금 0.0.21이 들어 있어 드라이버가 전혀 로드되지 않는 상태다.

(이후: probe score)

> 코드·설정 예시는 의도를 보여 주는 스케치다. 실제 구현은 SDK 헤더, IOGraphics 동작, 실측이 우선한다.
