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
| 1        | common      |     |        |        |      |
| 2        | notify      |     |        |        | notify_off_multiplier: |
| 3        | isr         |     |        |        |      |
| 4        | device      |     |        |        |      |
| 5        | pci cfg     |     |        |        |      |

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
