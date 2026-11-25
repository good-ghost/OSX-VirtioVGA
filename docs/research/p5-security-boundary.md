# P5 보안 경계 점검 (WI-5.005)

점검일: 2026-11-25, 대상 커밋: `2f96329` 이후 작업 트리(kext 0.0.41). 관련 요구: NFR-006, NFR-007.

방법: 장치·외부 입력을 읽는 곳을 모두 찾고(`deviceRead*`, `commonRead*`, `configRead*`, used 링, 응답 버퍼, `PE_parse_boot_argn`, `getProperty`, `setProperties`, `getConsoleInfo`), 각 값이 쓰이기 전에 어디서 범위 검사를 받는지 확인했다.

## 1. 호스트(QEMU)가 주는 값 — NFR-006

| # | 값 | 읽는 곳 | 검사 | 근거(테스트) |
| - | -- | ------- | ---- | ------------ |
| 1 | PCI capability 목록(위치·길이·BAR·offset) | `VirtioPciTransport::initWithDevice` | `vv::virtio::pci` 캡 순회(순환·범위 밖 포인터 차단), `buildLayout`이 영역이 BAR 크기 안인지 검사 | PciCapWalkTests 5, VirtioPciLayoutTests 8 |
| 2 | BAR 매핑 길이 | `mapRegion` | 영역 끝(offset+length) > 매핑 길이면 거절 | — |
| 3 | MMIO 접근 offset | `regionAddress` | offset+width > 영역 길이면 nullptr(읽기 0, 쓰기 실패) | — |
| 4 | `num_queues` | `setupQueues` | 2 미만이면 시작 실패 | — |
| 5 | `queue_size`(장치 최대) | `setupQueues`·`setupQueue` | `chooseQueueSize`로 원하는 크기와 최대 중 작은 2의 거듭제곱, 0이면 실패. 설정 전 다시 확인 | VirtioPciLayoutTests |
| 6 | `queue_notify_off` | `setupQueue` | `notifyOffsetFor`: notify_off × multiplier + 2 > notify 영역이면 거절 | VirtioPciLayoutTests |
| 7 | MSI-X 벡터 수락 | `setupQueue`·`setConfigVector` | 써 넣은 값을 다시 읽어 다르면(NO_VECTOR) 실패 | — |
| 8 | `device_status` | `checkStatus`·`handleConfigEvent` | `classifyStatus`: NEEDS_RESET·DRIVER_OK 없음 → 복구(횟수 제한 후 포기) | DeviceHealthTests |
| 9 | used 링 `id` | `SplitRing::collect` | id ≥ 큐 크기면 `DeviceError` → 복구 | SplitRingTests |
| 10 | used 링 `len`(쓴 길이) | `gatedSubmit` → `checkResponse` | 헤더보다 짧거나 기대 응답 크기보다 짧으면 실패. 복사는 항상 고정 크기(응답 버퍼 크기)만 | GpuCommandTests |
| 11 | 응답 type | `sendCommand` → `checkResponse` | 오류 type·예상과 다른 type 거절 | GpuCommandTests |
| 12 | `num_scanouts` | `queryDisplayInfo` | `usableScanouts`: 16(`kMaxScanouts`)으로 자름 | GpuCommandTests |
| 13 | display info 해상도 | `queryDisplayInfo` | `preferredModeFrom`: 꺼짐·0·상한 2560×1600 초과면 1280×800 | DisplayModeTests |
| 14 | EDID `size`·내용 | `queryEdid` | `edid::validate`: 1~1024, 128의 배수, 헤더, 블록 체크섬. 통과한 것만 `mEdid`(1024)에 복사 | EdidTests |
| 15 | config `events_read` | `handleConfigEvent` | DISPLAY 비트만 해석, 받은 값 그대로 `events_clear`에 써서 확인 | — |
| 16 | ISR | `intxFilter` | 비트만 검사 | — |

빠진 곳: **없음.**

## 2. 그 밖의 외부 입력

| 입력 | 들어오는 곳 | 검사 |
| ---- | ----------- | ---- |
| boot-args (`vvideo_*`) | 각 설정 함수 | 범위 밖이면 기본값: `vvideo_flush_ms` 4~1000, `vvideo_log` 0~2, `vvideo_cursor_scale` 50/100, `vvideo_dirty_tile` 16~256의 2의 거듭제곱 |
| Info.plist personality 값 | `cursorScaleSetting`, `loadDirtySettings` | 위와 같은 정규화 |
| 펌웨어 콘솔 정보(`getConsoleInfo`) | `copyBootScreen` | 크기·깊이(32)·rowBytes가 우리 모드와 맞을 때만 복사 |
| `setProperties`(VVDirtyFlush, VVDirtyTile) | `VirtioGpuDevice::setProperties` | root만(`kIOClientPrivilegeAdministrator`, 관리자 계정은 거절 실측), 딕셔너리·숫자 형식, 0/1, 타일은 정규화 결과와 같아야 함. 모르는 키만 있으면 `kIOReturnUnsupported` |
| IOGraphics 호출 (`setDisplayMode`, `getDDCBlock`, `setCursorImage`) | `VirtioVideoFramebuffer` | 모드 ID는 표에 있는 것만, 깊이는 하나만. DDC 블록 번호는 1부터, offset+128 ≤ EDID 크기. 커서는 64×64 설명자로 변환 후 `fits` 검사 |

## 3. user client — NFR-007

- `IOUserClient` 하위 클래스, `newUserClient`, `IOUserClientClass` 속성: **없음** (kext, kext-helper 소스와 Info.plist 검색).
- `IOUserClient.h`는 `clientHasPrivilege`(권한 확인 함수)를 쓰려고 포함한다. user client를 만들지 않는다.
- 상태는 IORegistry 속성으로만 내보낸다. 설정 변경 입구는 `setProperties` 하나이며 root만 받는다(위 표).

## 결론

NFR-006·NFR-007 모두 충족. 고칠 곳은 없었다.
