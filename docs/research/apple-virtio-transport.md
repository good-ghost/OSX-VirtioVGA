# AppleVirtIOPCITransport 공존 조사 (WI-1.001)

| 항목   | 내용                                                    |
| ------ | ------------------------------------------------------- |
| 날짜   | 2026-11-24                                              |
| 환경   | macOS 15.8.3 (24H241), QEMU 10.0.13 `virtio-vga`, OpenCore 1.0.6 |
| 관련   | REQ-003, R-02, T-04, PHASE-PLAN-01 WI-1.001             |
| 결론   | **공존(선택지 A)으로 진행한다.** 우리 드라이버는 별도 매칭 카테고리로 붙고, 초기화 시작 때 장치를 reset한다. 간섭이 관찰되면 kext 분리(선택지 C)로 바꾼다 |

## 1. Apple 드라이버의 매칭 정보

`com.apple.driver.AppleVirtIO` 230.100.3의 `Info.plist`:

- `AppleVirtIOPCITransport`: `IOProviderClass=IOPCIDevice`, `IOPCIPrimaryMatch=0x00001af4&0x0000FFFF` — 모든 virtio PCI 장치에 매칭된다.
- personality에 `IOMatchCategory`가 없다 → `IODefaultMatchCategory`.
- 실행 중 객체의 `IOProbeScore`는 10000 (personality에는 없으므로 `probe()`에서 정하는 값으로 보인다).
- AppleVirtIO는 부트 kernel collection에 있다 (로드 주소 `0xffffff80…`).
- GPU용 클라이언트 클래스는 없다. 같은 kext의 클라이언트는 9P, Balloon, Block, Console, Entropy, FS, Input, Network, Socket, Sound뿐이다. 이 VM에서는 console(`1af4:1043`)에만 클라이언트가 붙는다.

정적 분석: KDK의 `AppleVirtIO` 바이너리(x86_64 UUID `F63CEB9F-…`)에는 `AppleVirtIOPCITransport::start/probe/reset/getStatus` 등의 심볼이 있다. 하지만 링크 전 kext라 호출 대상이 vtable 오프셋으로만 보인다. 그래서 `start()`가 장치에 무엇을 쓰는지는 정적으로 확정하지 못했고, 아래 실측으로 판단했다.

## 2. 실측 1 — 공존 상태에서 장치 레지스터 읽기 (kext 0.0.6)

방법: 진단 서비스 `VirtioVideoPciProbe`를 별도 카테고리(`IOMatchCategory=VirtioVideoPciProbe`)로 GPU 장치에 붙였다. 레지스터는 읽기만 하고 아무것도 쓰지 않았다. 수집본: `env-20261124-002230-wi1001-coexist.txt`.

장치 아래 서비스: `AppleVirtIOPCITransport`(클라이언트 없음), `.Display_boot`(`IONDRVFramebuffer`), `VirtioVideoPciProbe`. 세 개가 동시에 붙고 부팅·화면에 문제가 없었다.

| 항목 | 값 | 해석 |
| ---- | -- | ---- |
| PCI ID / class | `1af4:1050`, class·rev `0x03000001` | virtio-vga |
| PCI command | `0x0007` | I/O·메모리·버스 마스터 켜짐 |
| MSI-X | cap `0xdc`, control `0x0002` | **꺼짐**, 테이블 3개 |
| `device_status` | `0x01` | **ACKNOWLEDGE만.** DRIVER·FEATURES_OK·DRIVER_OK 없음 |
| `driver_feature` (select 0) | `0` | 협상하지 않음 |
| `device_feature` (select 0) | `0x30000002` | 비트 1 EDID, 28 INDIRECT_DESC, 29 EVENT_IDX. 상위 32비트(VERSION_1 등)는 select에 써야 해서 읽지 않음 |
| `config_msix_vector` | `0xFFFF` | 벡터 미배정 |
| `num_queues` | 2 | controlq, cursorq |
| `queue_enable` (queue 0) | 0 | 큐 미설정 |
| GPU `num_scanouts` / `num_capsets` / `events_read` | 1 / 0 / 0 | |

virtio capability (설정 공간):

| 위치 | cfg_type | BAR | offset | length | 비고 |
| ---- | -------- | --- | ------ | ------ | ---- |
| 0x84 | 1 common | 2 | 0x1000 | 0x800 | |
| 0x94 | 3 isr | 2 | 0x1800 | 0x800 | |
| 0xa4 | 4 device | 2 | 0x2000 | 0x1000 | |
| 0xb4 | 2 notify | 2 | 0x3000 | 0x1000 | `notify_off_multiplier=4` |
| 0xc8 | 5 pci cfg | 0 | 0 | 0 | 설정 공간 창. BAR 매핑 대상 아님 |

BAR2 크기 0x4000(16 KiB)로 모든 영역이 BAR2 안에 있다. BAR0(8 MiB)은 VGA 프레임버퍼다.

해석: 누군가 장치를 ACKNOWLEDGE까지만 올려 두었고, 그 뒤로는 아무도 드라이버를 붙이지 않았다. ACKNOWLEDGE를 쓴 쪽이 Apple 전송 드라이버인지 펌웨어(OVMF/OpenCore)인지는 구분하지 못했다.

## 3. 실측 2 — Apple 드라이버를 밀어내기 시도 (kext 0.0.7)

방법: 진단 서비스를 `IODefaultMatchCategory`(personality에서 `IOMatchCategory` 삭제)로 옮기고 `IOProbeScore=20000`을 주었다. 수집본: `env-20261124-003013-wi1001-exclusive-attempt.txt`.

결과: **GPU 장치에는 여전히 `AppleVirtIOPCITransport`가 붙었고, `VirtioVideoPciProbe` 객체는 생기지 않았다.** kext 자체는 로드됐다 (0.0.7).

해석: 우리 kext는 `IOGraphicsFamily` 의존 때문에 시스템 kernel collection이 올라온 뒤에 로드된다 (T-04, WI-0.002 실측). 그때는 Apple 드라이버가 이미 기본 카테고리를 차지한 뒤라 점수 비교 기회가 없었다. 점수가 아니라 **로드 시점** 문제다.

## 4. 선택지 비교

| 선택지 | 방법 | 장점 | 단점·위험 |
| ------ | ---- | ---- | --------- |
| **A. 공존 (채택)** | 우리 전송 계층은 별도 카테고리로 붙고, 시작할 때 reset(status=0)부터 사양대로 초기화한다 | 지금 구조 그대로 된다 (실측 1에서 세 서비스가 함께 정상 동작). Apple 쪽은 ACK 이후 아무것도 하지 않는다 | Apple 드라이버가 나중에 (전원 상태 변화, 종료 등) 장치를 reset할 가능성을 배제하지 못했다 |
| B. 매칭 점수로 밀어내기 | 기본 카테고리 + 높은 점수 | Apple 드라이버를 장치에서 떼어 낸다 | **실측 2에서 실패.** 로드 시점 때문에 지금 구조로는 불가능 |
| C. kext 분리 | 전송·GPU 부분을 IOGraphicsFamily에 의존하지 않는 kext로 나눠 부트 단계에 로드시키고, B처럼 높은 점수로 장치를 차지한다. 프레임버퍼는 별도 kext/PlugIn | Apple 드라이버를 확실히 배제할 수 있다 (추정 — 시험하지 않음) | 구조가 복잡해진다. 부트 단계 매칭 순서가 실제로 우리에게 유리한지 확인하지 않았다 |
| D. Apple 비공개 API로 붙기 | `AppleVirtIOTransport` 클라이언트가 된다 | 전송 계층을 직접 만들지 않아도 된다 | Q-07에서 기각 (헤더 없음, 업데이트에 취약) |

## 5. 결정과 후속 조치

- **A로 진행한다** (REQ-003 충족: 충돌하지 않음을 실측 1로 확인, 회피 방법은 C로 기록).
- 우리 전송 계층(WI-1.003)은 시작할 때 반드시 `device_status=0` reset부터 한다. Apple 드라이버나 펌웨어가 남긴 ACK 상태를 가정하지 않는다.
- WI-1.003·WI-1.006 시험에서 확인할 것:
  - 우리가 DRIVER_OK까지 올린 뒤, 일정 시간(진단 속성 주기 갱신)과 디스플레이 잠자기·깨우기 후에도 `device_status`가 유지되는지.
  - 유지되지 않으면(외부 reset 관찰) C로 바꾸고 이 문서를 갱신한다.
- 진단 서비스 `VirtioVideoPciProbe`는 별도 카테고리로 되돌렸다 (kext 0.0.8, 아직 EFI에 설치하지 않음). WI-1.002 이후 본 전송 계층이 생기면 제거한다.
- T-04(로드 시점)는 P2의 부트 프레임버퍼 대체(WI-2.006)에도 같은 영향을 줄 수 있다. 프레임버퍼는 `IOFramebuffer` 카테고리라 `IONDRVFramebuffer`와 경쟁해야 하는데, 이미 붙은 IONDRV를 늦게 로드된 우리가 대체할 수 있는지는 별도로 확인해야 한다.
