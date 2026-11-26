# VirtioVGA Project Plan

English | [한국어](PROJECT-PLAN.md)

| Item      | Content                                           |
| --------- | ------------------------------------------------- |
| Baseline document | [REQUIREMENT.md](REQUIREMENT.en.md) v1.0 (2026-11-23) |
| Written   | 2026-11-23                                        |

## 1. Overview

Build a 2D framebuffer kext that drives the QEMU `virtio-vga` (`1af4:1050`) device in an OSX-KVM guest (macOS 15.8.3, x86_64). Currently the generic `IONDRVFramebuffer` uses a single 1280×800 mode and reports VRAM as "3 MB". This driver solves resolution selection (up to 2560×1600, including HiDPI) and the VRAM display (128 MiB).

Metal acceleration, the Sequoia wallpaper, and transparency effects are out of scope (REQUIREMENT §2.5.2, Q-12).

## 2. Schedule

No deadline or overall duration constraint was given. Only man-day estimates for a single developer are listed. Kernel debugging makes up a large share of the work, so the actual effort may be longer.

| Phase | Name                 | WIs   | Man-day | Cumulative | Milestone                                  |
| ----- | -------------------- | ----- | ------- | ----- | ------------------------------------------ |
| P0    | Development environment | 8  | 8.0     | 8.0   |                                            |
| P1    | virtio transport layer | 7   | 10.0    | 18.0  |                                            |
| P2    | 2D single-mode output | 10   | 12.5    | 30.5  | **M1**: the driver outputs the screen through virtio-gpu |
| P3    | Modes and VRAM       | 9     | 11.5    | 42.0  | **M2**: resolution selection + VRAM display (core goal) |
| P4    | Cursor, power, optimization | 5 | 7.0  | 49.0  |                                            |
| P5    | Stabilization and documentation | 10 | 9.5 | 58.5 | **M3**: stable version (passes AC-01~09) |
| P6    | Extensions (optional, Could) | 2 | 4.5 | 63.0  |                                            |
| Total |                      | 51    | 63.0    |       |                                            |

## 3. Working rules

- When a WI is finished: build and unit tests pass → mark `[X]` in the checklist and status table → commit. Detailed rules are in [CLAUDE.md (Korean)](../CLAUDE.md).
- Every step that installs the kext in the VM and boots it requires user confirmation each time. Before installing, check the rollback path (WI-0.003).
- WI IDs are not changed or reused. Inserted work uses a derived ID such as `WI-2.004b`.

## 4. Work Items by Phase

### P0. Development environment

| Status | ID        | Description                                         | Man-day |
| ------ | --------- | --------------------------------------------------- | ------- |
| [X]    | WI-0.001  | Decide build system and build an empty kext         | 1.5     |
| [X]    | WI-0.002  | Confirm minimal kext loading via OpenCore injection | 1       |
| [X]    | WI-0.003  | Rollback procedure and disabling boot-arg           | 1       |
| [X]    | WI-0.004  | KDK/QEMU gdbstub debugging and panic log collection procedure | 1.5 |
| [X]    | WI-0.005  | Logging macros and log levels                       | 0.5     |
| [X]    | WI-0.006  | User-space unit test harness                        | 1       |
| [X]    | WI-0.007  | Comparison mode for the environment collection script | 0.5   |
| [X]    | WI-0.008  | virtio / virtio-gpu constant and struct headers     | 1       |

- WI-0.001
  - [X] Recorded the result of checking whether Xcode 26.3 has a kext template
  - [X] A single run of `scripts/build.sh` produces `build/VirtioVGA.kext` (exit code 0)
  - [X] `OSBundleLibraries` in `Info.plist` has the IOGraphicsFamily and IOPCIFamily dependencies
  - [X] The `kmutil libraries -p build/VirtioVGA.kext` check finishes without errors
  - [X] The build command section of CLAUDE.md is filled in
- WI-0.002
  - [X] Documented the config.plist entry that injects, via OC `Kernel > Add`, a minimal kext that only logs without matching
  - [X] After reboot, this kext appears in `kmutil showloaded`
  - [X] One start log line is left in the boot log — confirmed through the IORegistry `VVLog` instead of the unified log (WI-0.005 decision)
  - [X] SIP state is the same as before the change (`csrutil status`)
- WI-0.003
  - [X] `docs/INSTALL.md` has the procedure to disable the OC entry (`Enabled=false`) and to restore a host snapshot
  - [X] With boot-arg `-vvideooff`, `probe()` returns `nullptr`
  - [X] Booted with the boot-arg and confirmed that `IONDRVFramebuffer` stays
- WI-0.004
  - [X] Recorded the result of installing the KDK for 24H241 (or the closest build) (exactly 24H241, UUID matches)
  - [X] Wrote in `docs/DEBUGGING.md` the procedure to attach lldb to the QEMU gdbstub and set breakpoints on kext symbols — connection, kext symbols, and memory reads confirmed; breakpoint hits to be confirmed in P1, when an execution path exists. Kernel symbols are not possible because lldb 19 crashes
  - [X] Wrote down the panic report location and collection commands (`scripts/panic-summary.py`, dSYM UUID archiving)
- WI-0.005
  - [X] Log macros exist for three levels: error/info/debug
  - [X] The level can be changed by a boot-arg or an Info.plist value, and the change was confirmed in the log
- WI-0.006
  - [X] In `tests/`, `scripts/test.sh` builds and runs kernel-independent code in user space
  - [X] One example test passes, and when made to fail on purpose the exit code is non-zero
- WI-0.007
  - [X] `collect-env.sh` also collects this driver's IORegistry properties
  - [X] There is a way (script or procedure) to compare two collection results
- WI-0.008
  - [X] The virtio common, PCI capability, split virtqueue, and virtio-gpu command/response structs are in headers
  - [X] Each constant has a comment with the specification section number
  - [X] Struct sizes are verified with `static_assert` and the unit tests pass

### P1. virtio transport layer

| Status | ID        | Description                                         | Man-day |
| ------ | --------- | --------------------------------------------------- | ------- |
| [X]    | WI-1.001  | Investigate coexistence with AppleVirtIOPCITransport | 1.5    |
| [X]    | WI-1.002  | PCI capability parsing and BAR mapping              | 1.5     |
| [X]    | WI-1.003  | Device initialization and feature negotiation       | 1       |
| [X]    | WI-1.004  | split virtqueue                                     | 2       |
| [X]    | WI-1.005  | MSI-X/INTx interrupts and polling mode              | 2       |
| [X]    | WI-1.006  | Device reset handling and transport class separation | 1      |
| [X]    | WI-1.007  | Transport layer integration check (GET_DISPLAY_INFO round trip) | 1 |

- WI-1.001
  - [X] Recorded the result of investigating whether the Apple transport driver touches the device status register (initialization/reset) (status=ACK only, queues/MSI-X/features not set)
  - [X] Chose one coexistence method (leave as is / adjust matching / claim first) with rationale — A (coexist). Pushing it out by score failed because of load timing (measured). Alternative C (split the kext) recorded
  - [X] Confirmed that the decision fits REQ-003 — booted with all three services attached at the same time, screen normal. Whether the state holds after our initialization is checked in WI-1.003
- WI-1.002
  - [X] Finds all vendor capabilities (`cfg_type` 1~5) and logs BAR, offset, and length (`VVVirtioCaps`, `VVLog`)
  - [X] Unit tests that reject a capability whose range exceeds the BAR size pass (`VirtioPciLayoutTests`, `PciCapWalkTests`)
  - [X] Recorded the values found on the real device in the measurement table (matches the WI-1.001 measurements and the 0.0.9 transport layer results)
- WI-1.003
  - [X] Proceeds in the order reset → ACKNOWLEDGE → DRIVER → feature → FEATURES_OK re-check → DRIVER_OK — confirmed on the real machine up to FEATURES_OK (status 0x0B). DRIVER_OK is written after queue setup (WI-1.004), per §3.1.1
  - [X] Fails if `VIRTIO_F_VERSION_1` is absent — confirmed by unit test (the real device offers VERSION_1, so this cannot be tested on the real machine)
  - [X] Does not negotiate 3D/blob features (`VIRGL`, `RESOURCE_BLOB`, `CONTEXT_INIT`) — deny list with static_assert, unit tests
  - [X] Leaves the features offered by the device and the negotiated features in the log and IORegistry (`VVDeviceFeatures`, `VVDriverFeatures`)
- WI-1.004
  - [X] Allocates the descriptor table and the avail/used rings in DMA-capable memory and sets their addresses on the device (`VVDmaBuffer` physically contiguous single segment, `setupQueue`, both queues enabled=1)
  - [X] Ring logic unit tests including index wrap-around pass (`SplitRingTests`, 70000 round trips)
  - [X] Sets the queue size to at most the value reported by the device (`chooseQueueSize`, device max 1024 → controlq 64, cursorq 16)
- WI-1.005
  - [X] Assigns 3 MSI-X vectors (config, controlq, cursorq) and receives interrupts — macOS gives this device only 1 MSI-X source, so implemented with vector 0 shared. The completion of the first WI-1.007 command was received by MSI-X interrupt (interrupts=1, completions=1)
  - [X] With the debug option that disables MSI-X, works with INTx + ISR — **cannot be tested on this device**: there is no legacy interrupt source (only IOPCIMessagedInterruptController). The code exists, and with `vvideo_irq=1` it falls back to Poll
  - [X] With the polling-mode debug option, works without interrupts — GET_DISPLAY_INFO round trip succeeded with `vvideo_irq=2` (interrupts=0, completions=1)
- WI-1.006
  - [X] On detecting `DEVICE_NEEDS_RESET`, takes the reset and re-initialization path (forced test in polling mode) — the same recovery path was confirmed on the real machine with the external reset simulation (Lost) of boot-arg `vvideo_test_reset=1`. The NEEDS_RESET bit check is covered by unit tests (there is no way to trigger this bit in QEMU)
  - [X] The transport layer is in classes separate from the GPU code (`VirtioPciTransport`, `VirtioQueue`, `VVDmaBuffer` / GPU is `VirtioGpuDevice`)
- WI-1.007
  - [X] A `GET_DISPLAY_INFO` request and response complete one round trip and 1280×800 for scanout 0 is logged (both MSI-X and Poll)
  - [X] Booting with the kext loaded, the screen is still normal through `IONDRVFramebuffer` (no matching yet) — QEMU keeps VGA output even after receiving commands

### P2. 2D single-mode output

| Status | ID        | Description                                         | Man-day |
| ------ | --------- | --------------------------------------------------- | ------- |
| [X]    | WI-2.001  | GPU command layer (serialization, timeout, response check) | 1.5 |
| [X]    | WI-2.002  | Display info query                                  | 0.5     |
| [X]    | WI-2.003  | Framebuffer memory pool and backing                 | 1.5     |
| [X]    | WI-2.004  | 2D resource lifecycle and pixel format              | 1.5     |
| [X]    | WI-2.005  | IOFramebuffer subclass (fixed mode)                 | 2.5     |
| [X]    | WI-2.006  | Boot framebuffer replacement (matching, probe score) | 1.5    |
| [X]    | WI-2.007  | Periodic flush timer                                | 1       |
| [X]    | WI-2.008  | Failure paths and resource release                  | 1       |
| [X]    | WI-2.009  | Boot screen handover                                | 1       |
| [X]    | WI-2.010  | virtio-vga-gl compatibility test                    | 0.5     |

- WI-2.001
  - [X] Command submission is serialized one at a time (IOCommandGate + 1 command in flight; calls from the work loop thread are rejected)
  - [X] The response wait timeout is defined as a constant, and on timeout an error is returned (`kCommandTimeoutMs`=1000, recovery after kIOReturnTimeout. The real device always responds, so a timeout could not be triggered on the real machine)
  - [X] On `RESP_ERR_*`, logs the command name and arguments (real machine: `RESOURCE_UNREF arg=0xfffffff0 -> ERR_INVALID_RESOURCE_ID`)
- WI-2.002
  - [X] Reads per-scanout enabled state and preferred size and checks the `num_scanouts` upper bound (16) (if the preferred size is 0, disabled, or above 2560×1600, falls back to 1280×800, `VVPreferredMode`)
- WI-2.003
  - [X] Allocates the mode size from a memory pool capped at 128 MiB (`VVFramebuffer` 1280×800 4,096,000 bytes)
  - [X] Determined by measurement, and recorded, the memory form usable for `getApertureRange()` (whether physically contiguous) (T-01) — physically contiguous allocation; 16 MiB also succeeds right after boot; no IOMMU translation
  - [X] Builds and sends the `ATTACH_BACKING` entry list — list builder (`vv/MemEntries.hpp`); sent successfully in the WI-2.004 test pattern (1 entry, 4,096,000 bytes)
- WI-2.004
  - [X] create → attach → set_scanout → transfer → flush → detach → unref runs without errors (real machine: 10 commands succeeded, 0 timeouts)
  - [X] The color pattern (red, green, blue, white) appears correctly (AC-04, screenshot) — the user confirmed red, green, blue, white from the left in the QEMU window. A host capture was not obtained
- WI-2.005
  - [X] Implements the required `IOFramebuffer` methods and reports a single 1280×800 mode
  - [X] `setGammaTable` returns success; reports that the hardware cursor is not supported
  - [X] WindowServer draws the desktop with this framebuffer (2026-11-24, kext 0.0.23 + NdrvBlocker 0.0.1)
- WI-2.006
  - [X] In `ioreg`, this driver is attached and `IONDRVFramebuffer` is absent (AC-01)
  - [X] Recorded the chosen probe score and rationale (score 30000. However, the actual replacement is done not by the score but by AAPL,iokit-ignore-ndrv from the helper kext)
- WI-2.007
  - [X] transfer + flush runs at a default 60 Hz; the interval can be changed by configuration (0.0.24: boot-arg `vvideo_flush_ms`, `VVFlushIntervalMs=16`, flush-errors 0)
  - [X] Window movement and typing are reflected on screen (~~recording~~) — judged by the user's visual check and captures. **Recording canceled (user decision 2026-11-25)**. Guest `screencapture -V` gives a capture error
- WI-2.008
  - [X] Even when failure is injected at each initialization step via a debug option, boot continues and `IONDRVFramebuffer` remains (AC-06) — 0.0.25: `vvideo_fail=1` (transport layer), `2` (device initialization), `3` (framebuffer) all return to IONDRV, no re-blocking, no panic
  - [X] With boot-arg `-vvideooff`, boots to the existing screen (AC-05) — 0.0.25: helper kext `skipped:-vvideooff`, 0 services of the main kext, IONDRV
- WI-2.009
  - [X] Confirmed by recording that when the driver takes over the screen, the Apple logo and progress bar do not break, or break at most once — 2026-11-24 user visual check "no black interruption" ("검게 끊기지 않음") (virtio-vga-gl boot, no recording). Boot screen copy is `copied` on virtio-vga, `skipped` on gl because of a size mismatch
- WI-2.010
  - [X] Tested whether 2D output works with `-device virtio-vga-gl` + `-display gtk,gl=on` and recorded the result (2026-11-24, kext 0.0.25: VIRGL offered but not negotiated, 1600×1000 output, 0 flush errors)

### P3. Modes and VRAM

| Status | ID        | Description                                         | Man-day |
| ------ | --------- | --------------------------------------------------- | ------- |
| [X]    | WI-3.001  | Mode table calculation                              | 1       |
| [X]    | WI-3.002  | Mode switching (setDisplayMode)                     | 2       |
| [X]    | WI-3.003  | EDID negotiation, validation, and provision         | 2       |
| [X]    | WI-3.004  | Merge preferred resolution and EDID modes           | 1       |
| [X]    | WI-3.005  | Display change event handling                       | 1       |
| [X]    | WI-3.006  | HiDPI modes                                         | 1.5     |
| [X]    | WI-3.007  | Restore selected mode                               | 1       |
| [X]    | WI-3.008  | VRAM reporting                                      | 1.5     |
| [X]    | WI-3.009  | Publish driver state in IORegistry                  | 0.5     |

- WI-3.001
  - [X] Unit tests pass that keep only the REQ-040 standard modes at or below 2560×1600 that fit in the pool (vv/ModeTable.hpp, 4 tests)
- WI-3.002
  - [X] Switching to each mode in System Settings > Displays actually changes the resolution (AC-03) — 0.0.26: 1280×800→1920×1080→2560×1600, confirmed by the user, system_profiler Resolution matches
  - [X] Releases the previous resource during the switch; no panic or hang (resource 1→2→3, 0 flush errors, no panic)
- WI-3.003
  - [X] EDID length and checksum validation unit tests pass (vv/Edid.hpp, 2 tests)
  - [X] Checked and recorded whether the display key in the wallpaper store changes to a value other than `FFFFFFFF-…` — 0.0.29: new key `2043E827-27F6-C3E9-C3EF-99363AC7F093` added, display name "QEMU Monitor", VendorID 18708 (0x4914), ProductID 4660 (0x1234)
- WI-3.004
  - [X] The preferred resolution is shown as the default mode; no duplicate modes (Default flag; if equal to a standard mode, the standard ID is used; unit tests)
- WI-3.005
  - [X] Checked whether `VIRTIO_GPU_EVENT_DISPLAY` arrives when the GTK window is resized, and if it does, updates the mode list — in the gl configuration, 2 events (`VVDisplayEvents=2`), display info and EDID re-queried, mode list rebuilt once. No events with plain virtio-vga + GTK (WI-1.005). Immediate reflection in IOGraphics and automatic switching are WI-6.001
- WI-3.006
  - [X] At 2560×1600, the 1280×800 HiDPI mode appears in System Settings and can be selected — macOS provides it itself from the mode list. system_profiler "UI Looks like 1280 x 800"
- WI-3.007
  - [X] After changing the mode and rebooting, it starts in the same mode — 2560×1600 HiDPI restored as is after reboots with 0.0.27 and 0.0.28
- WI-3.008
  - [X] `IOFBMemorySize` is 128 MiB (0.0.28: 134217728)
  - [X] The VRAM display in `system_profiler` is 128 MB and this driver appears in "Kernel Extension Info" (AC-02) — VRAM 128 MB confirmed. REQ-062 was changed in v1.4 to "'No Kext Loaded' disappears and the GPU name is shown" ("'No Kext Loaded'가 사라지고 GPU 이름이 표시된다") and is met in 0.0.51: no "No Kext Loaded", title and "Chipset Model: Virtio GPU" (PCI `model` property, 2026-11-25)
  - [X] Recorded the result of investigating the property used for the VRAM display — IOGraphics sets IOFBMemorySize from the getVRAMRange length and system_profiler uses that value. Overwriting the number is ignored
- WI-3.009
  - [X] Negotiated features, number of scanouts, current mode, queue statistics, and error counts are visible in IORegistry — VVDeviceFeatures/VVDriverFeatures, VVDisplayInfo, VVCurrentMode, VVControlQueue/VVEventCounters, VVScanout

### P4. Cursor, power, optimization

| Status | ID        | Description                                         | Man-day |
| ------ | --------- | --------------------------------------------------- | ------- |
| [X]    | WI-4.001  | Hardware cursor (cursorq)                           | 2       |
| [X]    | WI-4.002  | Display sleep/wake and lock screen                  | 1.5     |
| [X]    | WI-4.003  | Minimal handling of power state requests            | 0.5     |
| [X]    | WI-4.004  | Change-detection flush                              | 2       |
| [X]    | WI-4.005  | Measure optional virtqueue features                 | 1       |

- WI-4.001
  - [X] Cursor shape and position are updated through cursorq; confirmed by recording situations where the shape changes (text input, resizing) — 0.0.40: arrow, I-beam, window edge, and link hand shapes, size (50%) and click position confirmed by the user. `VVCursor images=9 moves=330 drops=0`. No recording (guest recording not possible, same constraint as WI-2.007)
  - [X] With the option that disables the hardware cursor, works with the software cursor — boot-args `keepsyms=1 -vvideoswcursor`: `get(crsr)=0`, `VVCursor images=0 moves=0`, 0 cursorq entries, the guest cursor is drawn on screen and changes shape (confirmed by the user), no panic
- WI-4.002
  - [X] No corruption, black screen, or panic over 10 repetitions of display sleep → wake → lock screen → login (AC-09) — 0.0.36, `scripts/display-sleep-cycles.sh`: 10/10 ok, no panic, screenshot after login 2560×1600. No recording (guest `screencapture -V` not possible, same constraint as WI-2.007)
  - [X] The flush timer stops during sleep (log, statistics) — `VVDisplayPower sleeps=11 wakes=11`, `VVScanout blanks=11 flush-errors=0`, "Display output is not active." in the host window during sleep
- WI-4.003
  - [X] Responds to system sleep requests without panic (restore functionality is out of scope, REQ-054 W) — `pmset sleepnow` twice (0.0.36, 0.0.37), no panic up to sleep in both. **Wake not tested**: a VM in S3 does not wake on key input and there is no QEMU monitor (`system_wakeup`). Once it went to FullWake by itself, but the screen and driver state could not be checked because of a restart
- WI-4.004
  - [X] Confirmed by statistics that the transfer volume drops when the screen is static — 0.0.41, static screen for 30 seconds: 25,188 MiB → 0 MiB (`VVDirtyStats`)
  - [X] Measured and recorded CPU usage with change detection on and off — guest `kernel_task` 6.1% → 24.9%, host QEMU 4.2~4.9% with no difference → default off
- WI-4.005
  - [X] Measured with `INDIRECT_DESC` and `EVENT_IDX` on and off and chose defaults — **decided by analysis without an on/off comparison measurement (user approval 2026-11-25)**: both default off. Rationale: commands are sent one at a time and their completion is awaited, so there are no completions to batch notifications for (EVENT_IDX), and at most 2 slots of the 64-slot queue are used (INDIRECT_DESC). Measured 101.5 commands/s, 102 interrupts/s, 0 timeouts

### P5. Stabilization and documentation

| Status | ID        | Description                                         | Man-day |
| ------ | --------- | --------------------------------------------------- | ------- |
| [X]    | WI-5.001  | 24-hour continuous use test                         | 1.5     |
| [X]    | WI-5.002  | 100 mode switches and leak test                     | 1       |
| [X]    | WI-5.003  | Check unload/reload feasibility                     | 1       |
| [X]    | WI-5.004  | Performance measurement                             | 1       |
| [X]    | WI-5.005  | Security boundary review                            | 1       |
| [X]    | WI-5.006  | Compatibility check (Lilu/WhateverGreen, macOS 15.x) | 0.5    |
| [X]    | WI-5.007  | Before/after comparison record of wallpaper and transparency effects | 1 |
| [X]    | WI-5.008  | Status query CLI                                    | 1       |
| [X]    | WI-5.009  | Installation/operation documents and license        | 0.5     |
| [X]    | WI-5.010  | Final acceptance criteria check                     | 1       |

- WI-5.001
  - [X] 0 panics over 24 hours of general work and video playback (NFR-001), result recorded — **tested for a reduced 12 hours and accepted together with its limits (user decision 2026-11-25)**: 0.0.42, 0 new panics during 12:19 of uptime, flush-errors, timeouts, device-errors, and recoveries all 0, no change in object counts, 3 display sleep/wake cycles normal. Limits: the screen was on and in use for about 43 minutes and the rest was in display sleep; wired memory +about 299 MiB (cause not identified)
- WI-5.002
  - [X] 0 panics or hangs over 100 mode switches, no increase in memory usage (NFR-002) — 0.0.41, `scripts/mode-switch-test.sh`: 17 modes (including HiDPI), all 100 ok, original mode restored, 0 panics, 0 flush-errors, 0 timeouts. Driver object counts the same before and after (VVDmaBuffer 6, VirtioQueue 2), wired memory 487,130 → 486,970 pages
- WI-5.003
  - [X] Checked whether `kmutil unload` of the OC-injected kext works with SIP on — the target changed to the auxiliary KC (SIP 0x201). `kmutil unload` while the screen is in use: **the VM froze in a panic state** (forced power-off, no report). NdrvBlocker is in the Boot KC, so it cannot be unloaded in the first place
  - [X] If it works, all resources are released on unload and reload succeeds; if not, that fact and an alternative were left as an NFR-003 change proposal — NFR-003 changed to "replace by rebooting" (REQUIREMENT v1.3, user approval)
- WI-5.004
  - [X] Measured the CPU usage increase at 1920×1080 (goal: at most 10% of one core, NFR-004) — `kernel_task` 4.6% (1920×1080), 3.8% (1280×800) vs IONDRV 1280×800 1.2~3.4% → increase of 0.4~3.4 percentage points, within the goal. IONDRV is fixed at 1280×800, so a 1920×1080-to-1920×1080 comparison is not possible
  - [X] Compared mouse and window drag latency against IONDRV by recording (NFR-005) — user visual comparison instead of recording: "same or better" ("같거나 더 낫다"). Guest recording not possible (same constraint as WI-2.007)
- WI-5.005
  - [X] Reviewed, as a list, the locations of range checks for every value given by the host (NFR-006) — all 16 locations have checks, none missing
  - [X] Confirmed that there is no user client (NFR-007) — no subclass, `newUserClient`, or `IOUserClientClass`. The only configuration entry point is `setProperties`, which accepts root only
- WI-5.006
  - [X] Confirmed no conflicts with Lilu and WhateverGreen loaded, and that WhateverGreen does not touch this device — all of P2~P4 (mode switching, sleep, cursor) carried out with Lilu 1.6.8 and WEG 1.6.7 loaded. Lilu's device classification excludes vendor 0x1af4 (checked in source); no traces of GFX0 rename, slot, or model. No macOS 15.x update (what is offered is 26.7.3, out of scope Q-05)
- WI-5.007
  - [X] Captured, measured, and recorded the five AC-07 items before and after — 0.0.42: ① photos normal ② Sequoia still not drawn (white fallback) ③ **solid 16 px border gone, same as the original color** ④ menu bar and Dock opaque ⑤ Metal 0. ②④⑤ are out of scope (Q-12)
- WI-5.008
  - [X] The CLI in `tools/` reads and prints the IORegistry state (no user client) — `tools/vvstat.c` (built and run with `scripts/vvstat.sh`): kext version (KextManager), framebuffer attachment, 12 key states, `--log N`, `--all`. Read-only, no root needed. Exit code 0 = driver-ok. The `VVEventCounters` truncation found while running it was fixed in 0.0.42
- WI-5.009
  - [X] `docs/INSTALL.md` and `docs/DEBUGGING.md` are up to date — INSTALL: auxiliary KC installation as the main procedure, one-time OpenCore setup, 12 boot-args, Info.plist defaults, disabling, recovery, and known constraints organized. DEBUGGING: `vvstat` interpretation table, list of panics during development. README updated
  - [X] Added an MIT `LICENSE` file with the copyright holder name chosen by the user — `Copyright (c) 2026 good-ghost`
- WI-5.010
  - [X] Re-checked all of AC-01~AC-09 and left a results table — re-checked AC-01, 02, 03, 04, 05, 06, 08 (NFR-002), and 09 on 0.0.42; AC-07 and AC-08 (NFR-001) use the 0.0.42 records. All pass (AC-08 accepted together with the NFR-001 limits) (`docs/ACCEPTANCE.md`)
  - [-] ~~Left a demo recording~~ — **canceled (user decision 2026-11-25)**. Guest recording not possible

### P6. Extensions (optional)

| Status | ID        | Description                                         | Man-day |
| ------ | --------- | --------------------------------------------------- | ------- |
| [X]    | WI-6.001  | Automatic resolution switching to fit the window size | 1.5   |
| [ ]    | WI-6.002  | Multiple scanouts (multiple monitors) — **on hold** | 3       |

- WI-6.001
  - [X] When the GTK window size changes, it switches automatically to a mode of that size, depending on the setting — 0.0.48 + agent `vvresize` (LaunchAgent): window size event → dynamic mode ID 100 → connection change notification → WindowServer switches to ID 100 (one flicker). User visual check (resolution matches, screen normal). No recording (guest recording not possible)
  - [X] There is a setting to turn off automatic switching — Info.plist `VVAutoResize` (default 1); in a boot with boot-arg `vvideo_autoresize=0`, confirmed that resizing the window keeps the list, 0 connection changes, and the resolution unchanged
- WI-6.002
  - [ ] With `max_outputs=2`, two monitors appear in System Settings and each produces output
  - On hold (2026-11-25, user decision): in the trial implementation (0.0.49), a second output enabled at boot was drawn, but WindowServer did not open an output enabled at runtime, and with two outputs there were keyboard failure, a hang on restart, and the screen turning off when the tab was detached. Reverted to 0.0.48; the trial code was not kept (deleted in the 2026-11-25 cleanup)

## 5. Requirements traceability

Requirements marked `W` (excluded from this scope) have no WI.

| Requirement | Priority | WI                           |
| -------- | ---- | ------------------------------- |
| REQ-001  | M    | WI-2.005, WI-2.006              |
| REQ-002  | M    | WI-2.006                        |
| REQ-003  | M    | WI-1.001                        |
| REQ-004  | M    | WI-2.008                        |
| REQ-005  | M    | WI-2.008                        |
| REQ-006  | M    | WI-0.001                        |
| REQ-007  | S    | WI-2.010                        |
| REQ-010  | M    | WI-1.002                        |
| REQ-011  | M    | WI-1.003                        |
| REQ-012  | M    | WI-1.003                        |
| REQ-013  | M    | WI-1.004                        |
| REQ-014  | M    | WI-1.005                        |
| REQ-015  | S    | WI-1.005                        |
| REQ-016  | C    | WI-4.005                        |
| REQ-017  | S    | WI-1.006                        |
| REQ-018  | S    | WI-1.006                        |
| REQ-020  | M    | WI-1.007, WI-2.002              |
| REQ-021  | M    | WI-2.004                        |
| REQ-022  | M    | WI-2.004                        |
| REQ-023  | M    | WI-2.001                        |
| REQ-024  | M    | WI-2.001                        |
| REQ-025  | S    | WI-3.003                        |
| REQ-026  | S    | WI-3.005                        |
| REQ-027  | M    | WI-1.003                        |
| REQ-030  | M    | WI-2.003                        |
| REQ-031  | M    | WI-2.005                        |
| REQ-032  | M    | WI-2.007                        |
| REQ-033  | S    | WI-4.004                        |
| REQ-034  | M    | WI-3.002                        |
| REQ-035  | S    | WI-2.009                        |
| REQ-036  | M    | WI-2.003, WI-3.001              |
| REQ-040  | M    | WI-3.001                        |
| REQ-041  | S    | WI-3.004                        |
| REQ-042  | M    | WI-3.002                        |
| REQ-043  | C    | WI-6.001                        |
| REQ-044  | S    | WI-3.006                        |
| REQ-045  | S    | WI-3.007                        |
| REQ-046  | C    | WI-6.002                        |
| REQ-050  | S    | WI-4.001                        |
| REQ-051  | M    | WI-2.005, WI-4.001              |
| REQ-052  | M    | WI-2.005                        |
| REQ-053  | M    | WI-4.002                        |
| REQ-054  | W    | (excluded) minimal handling only in WI-4.003 |
| REQ-060  | M    | WI-3.008                        |
| REQ-061  | M    | WI-3.008                        |
| REQ-062  | S    | WI-3.008                        |
| REQ-063  | S    | WI-1.003, WI-3.009              |
| REQ-064  | M    | WI-0.005                        |
| REQ-070  | M    | WI-0.002, WI-5.009              |
| REQ-071  | M    | WI-0.003                        |
| REQ-072  | M    | WI-0.003, WI-2.008              |
| REQ-073  | W    | (excluded)                      |
| REQ-074  | C    | WI-5.008                        |
| REQ-075  | M    | WI-0.007                        |
| NFR-001  |      | WI-4.003, WI-5.001              |
| NFR-002  |      | WI-5.002                        |
| NFR-003  |      | WI-5.003                        |
| NFR-004  |      | WI-4.004, WI-5.004              |
| NFR-005  |      | WI-5.004                        |
| NFR-006  |      | WI-1.002, WI-2.002, WI-3.003, WI-5.005 |
| NFR-007  |      | WI-5.005                        |
| NFR-008  |      | WI-5.006                        |
| NFR-009  |      | WI-5.006                        |
| NFR-010  |      | WI-0.006                        |
| NFR-011  |      | WI-0.008                        |
| NFR-012  |      | WI-0.008, WI-5.009              |
| NFR-013  |      | WI-0.004                        |

## 6. Technical points found during planning

The requirements were not changed. After measuring in the relevant WI, a requirement change is proposed if needed.

| ID   | Content                                                                                                                                    | Checked in WI |
| ---- | ------------------------------------------------------------------------------------------------------------------------------------------ | -------- |
| T-01 | **Resolved (2026-11-24, WI-2.003)**: the framebuffer is allocated physically contiguous. 2560×1600 (16 MiB) contiguous allocation also succeeds right after boot; device address = CPU physical address (no IOMMU). It does not conflict with REQ-030 (need not be contiguous), so the requirement is not changed. Remaining risk: a large contiguous allocation may fail due to fragmentation during runtime mode switches → checked in WI-3.002. Original content: | `IOFramebuffer::getApertureRange()` returns an `IODeviceMemory` (physical address range). The framebuffer may need to be physically contiguous, which could conflict with REQ-030 ("need not be physically contiguous" / "물리 연속일 필요 없음") | WI-2.003 |
| T-02 | It had not been confirmed whether NFR-003 (reload after unload) holds together with the Q-01 decision to use only OC injection — **resolved (2026-11-25)**: confirmed that unloading is not possible while the screen is in use; NFR-003 changed to replace-by-reboot | WI-5.003 |
| T-03 | REQ-043 is listed under both P3 and P6 in REQUIREMENT §11. It was placed in P6 (WI-6.001) to match priority C                               | —        |
| T-04 | An OC-injected kext goes into the boot kernel collection while IOGraphicsFamily is in the system collection, so this driver loads after the system collection loads (WI-0.002 measurement). Need to check whether this affects boot framebuffer replacement and boot screen handover **Confirmed 2026-11-24 (kext 0.0.21): a kext that uses IOGraphicsFamily symbols is not loaded by OC injection. The IOFramebuffer subclass needs another load path, such as the auxiliary collection** | WI-2.006, WI-2.009 |
| T-05 | AppleVirtIOPCITransport claims the GPU device first. Our kext loads late (T-04), so it cannot push it out by score in the same category (WI-1.001 measurement). Proceed with the coexistence approach, and switch to splitting the kext if an external reset is observed after our initialization | WI-1.003, WI-1.006 |
