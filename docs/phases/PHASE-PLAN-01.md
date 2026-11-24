# PHASE-PLAN-01 — virtio 전송 계층

| 항목      | 내용                                                         |
| --------- | ------------------------------------------------------------ |
| Phase     | P1                                                           |
| 기간      | 10.0 Man-day                                                 |
| 관련 요구 | REQ-003, 010~018, 020(일부), 027, 063(일부), NFR-006          |
| 선행      | P0 완료                                                      |
| 완료 조건 | 화면을 건드리지 않은 채 virtio-gpu와 명령 한 번(`GET_DISPLAY_INFO`)을 주고받는다 |

## 1. 목표

공개 API(`IOPCIDevice`)만으로 virtio 1.x modern PCI 전송 계층을 만든다 (Q-07 확정). 이 Phase에서는 **화면 출력을 맡지 않는다.** 드라이버는 장치에 붙되 `IOFramebuffer` 카테고리가 아닌 별도 카테고리로 붙어, 기존 `IONDRVFramebuffer` 화면을 유지한 채 시험한다. 이 방식이 Apple 전송 드라이버와 같은 장치를 다룰 때 안전한지는 WI-1.001에서 먼저 확인한다.

## 2. 실측 기준값 (REQUIREMENT §2.2, §8.1.1)

| 항목         | 값                                                         |
| ------------ | ---------------------------------------------------------- |
| PCI          | 07:00.0, `1af4:1050`, class `0x030000`                     |
| BAR0         | 8 MiB, VGA 프레임버퍼                                       |
| BAR2         | 16 KiB prefetchable — modern capability 영역으로 추정       |
| BAR4         | 4 KiB — MSI-X 테이블로 추정                                 |
| MSI-X 벡터   | 3 (`vectors` 기본값)                                        |
| 제공 feature | `VERSION_1`, `EDID`, `INDIRECT_DESC`, `EVENT_IDX`, `RING_RESET` 예상 (실측으로 확정) |

WI-1.002에서 아래 표를 채운다.

| cfg_type | 이름        | BAR | offset | length | 비고 |
| -------- | ----------- | --- | ------ | ------ | ---- |
| 1        | common      | 2   | 0x1000 | 0x800  | 설정 공간 0x84 |
| 2        | notify      | 2   | 0x3000 | 0x1000 | notify_off_multiplier: 4 (0xb4) |
| 3        | isr         | 2   | 0x1800 | 0x800  | 0x94 |
| 4        | device      | 2   | 0x2000 | 0x1000 | 0xa4 |
| 5        | pci cfg     | 0   | 0      | 0      | 0xc8, 설정 공간 창 |

(WI-1.001 실측, kext 0.0.6. MSI-X cap 0xdc, 테이블 3, 부팅 직후 꺼짐. 장치 제공 feature 하위 32비트 `0x30000002`.)

## 3. 의존 관계와 실행 순서

```
WI-1.001 ─> WI-1.002 ─> WI-1.003 ─> WI-1.004 ─> WI-1.005 ─> WI-1.007
                                         └────────> WI-1.006 ─┘
```

WI-1.001이 "공존 불가"로 끝나면 이후 WI의 매칭 방식을 바꾸고 이 문서에 기록한 뒤 진행한다.

## 4. Work Item 상세

### WI-1.001 AppleVirtIOPCITransport 공존 조사 (1.5)

배경: `com.apple.driver.AppleVirtIO`의 `AppleVirtIOPCITransport`가 `IOPCIPrimaryMatch 0x00001af4&0x0000FFFF`로 모든 virtio 장치에 붙는다. 현재 GPU 장치에도 붙어 있지만 하위 클라이언트가 없다 (REQUIREMENT §2.3).

세부 작업

1. `ioreg -l`로 Apple 전송 객체의 속성(장치 상태, feature, 큐 정보로 보이는 키)을 기록한다.
2. 이 객체가 붙은 상태에서 device status 레지스터 값을 읽어 본다 (우리 kext에서 읽기만).
3. 선택지를 비교해 정한다.
   - A) 그대로 두고 우리가 reset 후 초기화한다 — Apple 쪽이 이후 레지스터를 건드리지 않는지 관찰 필요
   - B) 우리 personality의 probe score를 높여 Apple 전송보다 먼저 같은 카테고리로 점유한다
   - C) 기타 (조사 결과에 따라)
4. 결과를 `docs/research/apple-virtio-transport.md`에 쓴다.

수락 기준: 선택지와 근거가 문서에 있고, 선택한 방식으로 부팅·장치 초기화가 1회 성공한다.

### WI-1.002 PCI capability 파싱과 BAR 매핑 (1.5)

```cpp
// 스케치: capability 목록 순회
for (UInt32 off = pci->extendedFindPCICapability(kIOPCICapabilityIDVendorSpecific, &cap); …) {
    VirtioPciCap c = readCap(pci, off);
    if (!isWithinBar(c, barSize(c.bar))) { return kIOReturnBadArgument; } // NFR-006
}
IOMemoryMap *map = pci->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0 + 4 * bar);
```

- 범위 검사 로직은 `kext/include/`에 순수 함수로 두고 단위 테스트한다.
- 수락 기준: 실측표(§2)를 채우고, 범위 초과 capability 거절 테스트 통과.

### WI-1.003 장치 초기화와 feature 협상 (1)

순서 (VIRTIO 1.2 §3.1.1):

1. `device_status = 0` (reset), 0이 될 때까지 대기 (타임아웃 상수)
2. `ACKNOWLEDGE`, `DRIVER`
3. `device_feature_select` 0/1로 64비트 feature 읽기
4. 우리가 지원하는 집합과 교집합. **`VERSION_1` 필수**, `VIRGL`·`RESOURCE_BLOB`·`CONTEXT_INIT` 제외 (REQ-027)
5. `FEATURES_OK` 쓰고 다시 읽어 확인
6. 큐 설정(WI-1.004) 후 `DRIVER_OK`

수락 기준: 각 단계 로그, 제공/협상 feature를 IORegistry 속성 `VVDeviceFeatures`, `VVDriverFeatures`(이름은 구현 시 확정)로 공개.

### WI-1.004 split virtqueue (2)

- 큐 0(controlq), 큐 1(cursorq). 크기는 `queue_size` 이하에서 상수로 정한다.
- 메모리: `IOBufferMemoryDescriptor` + `IODMACommand`로 DMA 주소 획득. 정렬(디스크립터 16, avail 2, used 4 바이트)을 지킨다.
- 알림: notify 주소 = notify BAR + `cap.offset + queue_notify_off × notify_off_multiplier` (`page-per-vq=off`).
- 단위 테스트: 디스크립터 free list, avail/used 인덱스 wrap-around(65535→0), used 링 소비.

### WI-1.005 MSI-X·INTx 인터럽트와 폴링 모드 (2)

- MSI-X: `config_msix_vector` = 0, controlq = 1, cursorq = 2. 쓰기 후 다시 읽어 `NO_VECTOR`(0xFFFF)면 실패 처리.
- 인터럽트 소스: `IOFilterInterruptEventSource`로 필터 → 워크루프에서 used 링 처리.
- INTx: ISR 레지스터 읽기로 원인 확인(읽으면 지워짐).
- 폴링: 디버그 boot-arg로 타이머 기반 used 링 확인.
- 수락 기준: 세 모드 각각에서 WI-1.007 왕복 성공.

### WI-1.006 장치 reset 처리와 전송 클래스 분리 (1)

- 클래스 구성(스케치): `VirtioPciTransport`(capability, status, feature, 큐, 인터럽트) / `VirtioGpuDevice`(명령) / `VirtioVideoFramebuffer`(IOFramebuffer, P2).
- `DEVICE_NEEDS_RESET`(status bit 64) 감지 → reset → 재초기화 → 상위 계층에 통지.

### WI-1.007 전송 계층 통합 확인 (1)

- `VIRTIO_GPU_CMD_GET_DISPLAY_INFO` 1회 제출 → `RESP_OK_DISPLAY_INFO` 확인 → scanout 0의 `r.width/height`(1280×800 예상)와 `enabled` 로그.
- 수락 기준: 위 로그, 화면은 기존 `IONDRVFramebuffer`로 정상, 10회 재부팅에서 같은 결과.

## 5. 검증 흐름

AI가 코드와 단위 테스트를 만들고 빌드·테스트 종료 코드를 확인한다 → 사용자가 EFI에 설치·재부팅한다 → AI가 로그·ioreg로 수락 기준을 판정한다. 부팅 실패 시 WI-0.003 절차로 되돌리고 패닉 로그를 수집한다.

## 6. 리스크

| 리스크                                              | 대응                                       |
| --------------------------------------------------- | ------------------------------------------ |
| Apple 전송과 충돌 (R-02)                            | WI-1.001을 첫 작업으로 진행                |
| BAR2 prefetchable 매핑에서 캐시 속성 문제           | 매핑 옵션을 uncached로 지정, 실측 확인     |
| MSI-X 활성화가 Apple 전송과 겹침                    | INTx·폴링 경로로 우선 진행, 원인 기록      |

> 코드 예시는 의도를 보여 주는 스케치다. 실제 API 이름·시그니처는 SDK 헤더(`IOPCIDevice.h`)와 실측이 우선한다.

## 7. 결정 기록

### WI-1.001 (2026-11-24)

- 공존(A)으로 진행. 근거와 실측은 [docs/research/apple-virtio-transport.md](../research/apple-virtio-transport.md).
- 부팅 직후 장치 상태: `device_status=ACK(1)`, feature·큐·MSI-X 미설정. WI-1.003은 reset부터 시작한다.
- 같은 카테고리·높은 점수로 Apple 드라이버를 밀어내는 방식은 로드 시점 때문에 실패했다 (T-05). 진단 서비스는 별도 카테고리로 되돌렸다 (kext 0.0.8, 미설치).
- `vv/PciCapWalk.hpp`(capability 순회·검증)를 이번에 만들었다. WI-1.002는 이것을 전송 계층에 연결하는 작업이 된다.
- 빌드: `IOPCIDevice`가 "Use PCIDriverKit"으로 deprecated라 `-Wno-error=deprecated-declarations`를 추가했다 (경고는 그대로 보인다).

### WI-1.002 (2026-11-24)

- `vv/VirtioPciLayout.hpp`: capability 목록에서 common/notify/isr/device를 종류마다 "BAR 범위 안, 최소 길이 충족"인 첫 것으로 고른다 (§4.1.4의 SHOULD). 최소 길이: common 56바이트(VIRTIO 1.0 필드 끝), isr 1, notify 2, device는 장치별(GPU 16). `notify_off_multiplier`는 0 또는 짝수 2의 거듭제곱만 허용. 큐 알림 오프셋 경계 계산 포함.
- `VirtioPciTransport`(OSObject): 레이아웃을 고르고 영역이 있는 BAR를 `kIOMapInhibitCache`로 한 번씩 매핑한다. 매핑 길이도 영역 끝과 다시 비교한다. 레지스터 읽기는 영역 경계를 넘으면 0과 실패를 돌려준다. 쓰기는 WI-1.003에서 더한다.
- 진단 서비스(`VirtioVideoPciProbe`)가 이 전송 계층을 쓰도록 바꿨다.
- 실기 확인 (kext 0.0.9, 수집본 `env-*-wi1002-transport.txt`):
  - `VVLayoutResult = none`, `VVLayout = common=2:0x1000 notify=2:0x3000 isr=2:0x1800 device=2:0x2000 mult=4` — WI-1.001 실측표와 같다.
  - common 영역 끝을 넘는 32비트 읽기(`length-2`)가 거절됨 (`VVBoundsCheckRejected = Yes`).
  - 장치 상태는 이전과 같다 (status=ACK, nq=2, scanouts=1). 부팅·화면 정상.
- 단위 테스트 23개 통과.

### WI-1.003 (2026-11-24)

구현

- `vv/VirtioFeatures.hpp`: 필수 `VERSION_1`, 선택 `ACCESS_PLATFORM`·GPU `EDID`, 금지 `VIRGL`·`RESOURCE_BLOB`·`CONTEXT_INIT`·`RESOURCE_UUID` (겹치지 않음을 static_assert). 링 feature(`INDIRECT_DESC`, `EVENT_IDX`, `RING_RESET`, `RING_PACKED`)는 아직 받지 않는다 (WI-4.005에서 측정 후).
- `VirtioPciTransport`: `reset()`(0 쓰고 0이 읽힐 때까지 최대 1초), `addStatus()`, 64비트 feature 읽기·쓰기.
- `VirtioGpuDevice`: 진단 서비스 `VirtioVideoPciProbe`를 대신하는 장치 서비스 (`IOMatchCategory=VirtioVideoGpu`). 초기화 실패 시 FAILED → reset. P1 동안은 실패해도 객체를 남겨 원인을 속성으로 보인다 (P2에서는 REQ-005대로 false).
- 2초 주기 상태 감시: 기대값과 다르면 `VVStatusMismatches`를 올리고 로그를 남긴다 (T-05).
- `stop()`에서 장치를 reset한다.

실기 확인 (kext 0.0.10, UUID `FB5DA7D5-…`)

| 항목 | 값 |
| ---- | -- |
| reset 전 상태 | `0x01` (ACK) — WI-1.001 관찰과 같다 |
| 장치 feature | hi `0x00000001`, lo `0x30000002` → 비트 1 EDID, 28 INDIRECT_DESC, 29 EVENT_IDX, 32 VERSION_1 |
| 협상 결과 | hi `0x00000001`, lo `0x00000002` → VERSION_1, EDID. 읽어 들인 값이 쓴 값과 같다 |
| FEATURES_OK 후 상태 | `0x0B` (ACK, DRIVER, FEATURES_OK) |
| 상태 감시 | 부팅 후 약 5분, 138회 확인, 불일치 0 — 이 시간 동안 외부에서 장치를 reset하지 않았다 (T-05). 디스플레이 잠자기·DRIVER_OK 이후는 WI-1.006·WI-4.002에서 다시 본다 |
| 화면·부팅 | 정상 (IONDRV 1280×800), 새 패닉 없음 |

관찰

- QEMU `virtio-vga,help`에는 `queue_reset=on`이 있지만 장치는 `VIRTIO_F_RING_RESET`(비트 40)을 제공하지 않았다. `ACCESS_PLATFORM`(33)도 없다 (`iommu_platform=off`와 일치).
- `VERSION_1` 없는 장치에서 실패하는 경로는 실제 장치로 만들 수 없어 단위 테스트로만 확인했다.

### WI-1.004 (2026-11-24)

구현

- `vv/SplitRing.hpp`: 디스크립터 빈 목록, 체인 등록(add)과 회수(collect), avail idx 공개 전 메모리 배리어(§2.7.13), 장치의 범위 밖 used id를 `DeviceError`로 구분. desc·avail·used를 한 블록에 정렬(16/2/4)해 배치하는 `ringLayout`.
- `VVDmaBuffer`: page 단위 물리 연속 할당(`kIOMemoryPhysicallyContiguous`) + `IODMACommand`(64비트, kMapped)로 장치 주소 1세그먼트. 프레임버퍼(T-01)에도 쓸 예정.
- `VirtioQueue`: 큐 하나의 DMA 메모리와 링. `VirtioPciTransport::setupQueue`: queue_select → 크기·MSI-X 벡터(NO_VECTOR, WI-1.005 전까지)·세 주소(64비트는 32비트 두 번) → 마지막에 queue_enable=1, 다시 읽어 확인. 알림 오프셋을 notify 영역 경계로 검사.
- `VirtioGpuDevice`: controlq 64, cursorq 16(장치 최대 이하 2의 거듭제곱) 등록 후 DRIVER_OK. 종료·실패 때는 reset을 먼저 하고 큐 메모리를 놓는다.
- `build.sh`: kext가 쓰는 KPI와 `OSBundleLibraries` 선언을 비교해 빠지면 실패한다. 이번에 `kernel_task` 때문에 `com.apple.kpi.mach`가 필요했는데 선언이 빠져 있었다 (빌드는 됐지만 로드 때 심볼 해석에 실패할 수 있었다).

실기 확인 (kext 0.0.11, UUID `9D7194ED-…`, 수집본 `env-*-wi1004-driver-ok.txt`)

| 항목 | 값 |
| ---- | -- |
| 장치 상태 | `0x0F` (ACK, DRIVER, FEATURES_OK, DRIVER_OK) |
| controlq | 크기 64, 장치 최대 1024, notify_off 0 → 오프셋 0x0, enabled=1 |
| cursorq | 크기 16, 장치 최대 1024, notify_off 1 → 오프셋 0x4, enabled=1 |
| 화면 | DRIVER_OK 뒤에도 IONDRV 1280×800 그대로. 명령을 보내지 않은 상태에서는 VGA 출력이 유지됐다 |
| 상태 감시 | 부팅 후 약 4분, 122회 확인, 불일치 0 (DRIVER_OK 상태에서도 외부 reset 없음, T-05) |
| 패닉 | 없음 |

관찰

- 장치 최대 큐 크기는 두 큐 모두 1024다 (계획서의 예상 256과 다름).
- 큐 주소는 물리 주소 성격이라 IORegistry에 공개하지 않았다 (크기·오프셋만).
- 장치 쪽이 링을 실제로 읽고 쓰는 것은 명령을 보내는 WI-1.007에서 처음 확인된다.

### WI-1.005 (2026-11-24, 구현 완료·수신 확인은 WI-1.007)

실측

- `IOPCIDevice::configureInterrupts`는 MSI-X(3개)와 레벨 둘 다 `0xe00002c7`(kIOReturnUnsupported)을 돌려준다 (kext 0.0.12). 실패 경로에서 장치는 FAILED → reset되고 화면은 IONDRV로 유지됐다.
- GPU 장치의 인터럽트 소스는 `IOInterruptControllers = (IOPCIMessagedInterruptController)`, 인덱스 0 하나(`0:msix`)뿐이다. PCI 계층이 MSI-X 벡터 1개를 미리 구성해 두며, 레거시(INTx) 소스는 없다.

구현 (kext 0.0.13~0.0.14)

- `configureInterrupts`를 부르지 않고 이미 있는 소스를 쓴다. `vv/VirtioIrq.hpp`의 `effectiveIrqMode`: MSI-X 소스가 있으면 MSI-X, 없고 레거시가 있으면 INTx, 둘 다 없으면 Poll. MSI-X 소스가 3개 미만이면 config·controlq·cursorq가 벡터 0을 공유하고(§4.1.5.1.2 허용), 인터럽트마다 config 이벤트와 두 큐를 모두 확인한다.
- boot-arg `vvideo_irq=0|1|2`(MSI-X/INTx/Poll). 이 장치에서 1(INTx)을 주면 레거시 소스가 없어 Poll로 내려간다.
- 서비스 전용 워크루프에서 인터럽트·타이머를 직렬화. INTx 필터는 1차 문맥에서 ISR만 읽고 원인을 원자적으로 쌓는다.
- 진단 속성: `VVIrqMode`, `VVIrqSharedVector`, `VVInterruptSources`, `VVEventCounters`, 2초마다 `VVGpuEventsReadNow`·`VVMsixControlNow`·`VVPciCommandNow`.

실기 확인 (kext 0.0.14)

| 항목 | 값 |
| ---- | -- |
| 방식 | `msix`, 공유 벡터 0, 이벤트 소스 1개 |
| 큐 벡터 | controlq·cursorq 모두 `vector=0x0`, 장치가 수락(다시 읽기 일치) |
| MSI-X 제어 (활성 후) | `0x8002` — 활성 비트 켜짐, 테이블 3 |
| PCI command | `0x0407` — INTx 비활성 비트 켜짐 |
| 상태 | `0x0F`, 불일치 0 |
| QEMU 창 크기 변경 후 | `events_read=0`, 인터럽트 0 |

결론

- 인터럽트 설정은 장치·PCI 계층 모두 기대대로다. 실제 인터럽트 수신은 아직 한 번도 일어나지 않았다.
- QEMU GTK 창 크기 변경으로는 디스플레이 이벤트가 생기지 않았다. virtio-gpu scanout이 아직 없는(VGA 출력 중인) 상태라서일 가능성이 있다 (확인 못 함). config 인터럽트는 scanout이 생기는 P2 이후(WI-3.005)에 다시 본다.
- controlq 인터럽트와 Poll 방식은 WI-1.007의 첫 `GET_DISPLAY_INFO` 왕복으로 확인한다. 그때 WI-1.005 체크리스트를 닫는다.

### WI-1.007 (2026-11-24) — WI-1.005 수신 확인 포함

구현 (kext 0.0.15)

- `vv/GpuCommand.hpp`: 응답 검증(헤더보다 짧음 → 오류 type → 기대 크기보다 짧음 → type 불일치 순서. 오류 응답은 헤더만 오므로 type을 길이보다 먼저 본다), scanout 수를 16으로 자르기. 테스트 5개.
- `VirtioPciTransport::notifyQueue`: 배리어 후 notify 주소에 큐 번호를 쓴다.
- `VirtioGpuDevice::submitCommand`: `IOCommandGate` 안에서 요청(오프셋 0)·응답(오프셋 1024)을 한 page DMA 버퍼에 두고 2조각 체인으로 controlq에 올려 notify, `commandSleep`(기한 1초)으로 대기. 인터럽트·Poll 처리기가 used 링에서 같은 head를 회수하면 `commandWakeup`. 시간 초과면 명령이 장치에 남은 것으로 보고 이후 명령을 막는다(reset은 WI-1.006).
- DRIVER_OK 직후 `GET_DISPLAY_INFO` 1회.

실기 확인

| 방식 | boot-args | 결과 |
| ---- | --------- | ---- |
| MSI-X (공유 벡터 0) | `keepsyms=1` | `VVDisplayInfo = 0:1280x800`, scanout 0 `enabled=1 1280x800+0+0 flags=0x0`. `interrupts=1 controlq=1 completions=1 commands=1 timeouts=0` |
| Poll (10ms) | `keepsyms=1 vvideo_irq=2` | `VVDisplayInfo = 0:1280x800`. `interrupts=0 controlq=1 completions=1 commands=1 timeouts=0` |

- 두 경우 모두 status `0x0F`, 상태 불일치 0, 새 패닉 없음.
- **명령을 처리한 뒤에도 QEMU 화면은 VGA 출력(IONDRV 1280×800)을 유지했다.** 우려했던 검은 화면은 생기지 않았다. QEMU가 virtio-gpu 출력으로 바꾸는 시점은 `SET_SCANOUT` 등 이후 명령에서 확인한다 (P2).
- 수집본: `env-*-wi1007-msix-roundtrip.txt`, `env-*-wi1007-poll-roundtrip.txt`.
- PHASE-PLAN의 "10회 재부팅" 반복은 하지 않았다. 이번 Phase 동안 재부팅 약 15회에서 초기화 실패 1회(0.0.12, `configureInterrupts` 미지원 — 원인 수정됨) 외에는 같은 결과였다.
- 남은 설정: OpenCore boot-args가 시험용 `keepsyms=1 vvideo_irq=2`(Poll)이다. 다음 EFI 작업 때 `keepsyms=1`로 되돌린다.

