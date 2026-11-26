# VirtioVGA Requirements Specification

English | [한국어](REQUIREMENT.md)

## 1. Document information

| Item      | Content                                                      |
| --------- | ------------------------------------------------------------ |
| Document  | `docs/REQUIREMENT.md`                                        |
| Version   | 1.4                                                          |
| Created   | 2026-11-23                                                   |
| Status    | Final — all Open Questions answered. Later changes are recorded in the change history |
| Target    | virtio-gpu 2D framebuffer driver (kext) for macOS guests on OSX-KVM |

### 1.1 Change history

| Version | Date       | Type | Content   |
| ---- | ---------- | ---- | --------- |
| 0.1  | 2026-11-23 | ADD  | Initial version |
| 0.2  | 2026-11-23 | ENHANCE | Per the user instruction "Use the recommendations", Q-01·02·03·05·06·07·09·10 were finalized with the recommended options. Applied to REQ-036·040·061·073, NFR-008, §4.2, §7, §9.2. Q-04·08 are information requests and stay open |
| 0.3  | 2026-11-23 | ENHANCE | Added §8.1 from host information provided by the user (QEMU 10.0.13, `-device` list); supplemented HST-002·A-2; applied a partial answer to Q-04 |
| 0.4  | 2026-11-23 | ENHANCE | Q-08 answered with the user's capture. Added §2.5 (wallpaper symptom measurements); supplemented §2.4-3·AC-07·R-03; added Q-11 |
| 0.5  | 2026-11-23 | ENHANCE | Applied the Q-11 test results: a photo wallpaper (Sonoma.heic) is shown without a border, the menu bar is still opaque. Added the user report (set a photo but a solid color is shown) and the display UUID observation to §2.5. Supplemented REQ-025·AC-07 |
| 0.6  | 2026-11-23 | ENHANCE | Cause identified (§2.5.2) from the user report (Sequoia photo → shown as solid Cyan) and the Metal device check. Revised G-5·R-03, Q-11 answered, added Q-12 |
| 0.7  | 2026-11-23 | ENHANCE | Added the §8.1 default-value table from the `virtio-vga,help` output provided by the user. Confirmed the cause of 1280×800; supplemented HST-002·A-2·REQ-014·REQ-016·REQ-036 |
| 0.8  | 2026-11-23 | ENHANCE | User information: display backend is GTK; with `-display gtk,gl=on`, `virtio-vga-gl` can be used. Added §8.1.2; supplemented scope·REQ-007·REQ-027·HST-001·HST-004·Q-12; added Q-13 |
| 0.9  | 2026-11-23 | ENHANCE | Per the user instruction "Use recommendations", finalized Q-12 (A+B) and Q-13 (A). Applied to §4.2, §8.1.2, R-03 |
| 1.0  | 2026-11-23 | ENHANCE | Finalized Q-04 with the answer (system sleep not used; switches to the login screen after some time). REQ-053 priority M, REQ-054 W; added AC-09·R-09; added §8.1.3 |
| 1.1  | 2026-11-24 | ENHANCE | **Q-01 changed** (user approval "Yes, try it"): following the confirmation of T-04 (a kext that uses IOGraphicsFamily symbols cannot be loaded by OC injection), switched to testing installation into `/Library/Extensions` (auxiliary kernel collection) with partial SIP relaxation (`csr-active-config` 0x201). REQ-070·071·073 and INSTALL.md will be revised after seeing the test results |
| 1.2  | 2026-11-24 | ENHANCE | Q-01 change finalized after a successful test: SIP 0x201 + `/Library/Extensions` (auxiliary kernel collection) + `kmutil load -z`. Changed the REQ-070·071 install and rollback paths to this method |
| 1.3  | 2026-11-25 | ENHANCE | **NFR-003 changed** (user approval "Change to reboot"): `kmutil unload` while the display is in use leaves the VM stopped in a panic state (WI-5.003). Replaced "unload then reload" with "replace by rebooting" |
| 1.4  | 2026-11-25 | ENHANCE | **REQ-062 changed** (user approval: "change the requirement wording and use the card name 'Virtio GPU'" (요구사항 문구 변경하고, 카드이름 'Virtio GPU' 사용)): `SPDisplaysReporter` contains only the string "No Kext Loaded" as a value of "Kernel Extension Info", and appears to show that line only when the framebuffer is IONDRV, so the driver name cannot be displayed. Changed to "No Kext Loaded" disappearing and the GPU name being shown |

### 1.2 Terms

| Term            | Meaning                                                                                |
| --------------- | -------------------------------------------------------------------------------------- |
| Guest           | macOS running on QEMU/KVM (the side where the driver covered by this document is installed) |
| Host            | Linux + QEMU running OSX-KVM                                                            |
| virtio-vga      | QEMU device. Has both a VGA-compatible legacy framebuffer (BAR0) and virtio-gpu functionality |
| scanout         | One output that virtio-gpu sends to a screen (monitor)                                 |
| resource        | A 2D image buffer managed by virtio-gpu. Used with guest memory (backing) attached      |
| controlq / cursorq | virtio-gpu command queue (queue 0) / cursor queue (queue 1)                         |
| GOP             | UEFI Graphics Output Protocol. Single-resolution framebuffer set up by OVMF at boot     |
| IONDRVFramebuffer | Generic framebuffer driver in Apple `IONDRVSupport.kext`. Uses the GOP screen as-is when no dedicated driver exists |
| OC              | OpenCore boot loader                                                                   |
| KDK             | Apple Kernel Debug Kit. Kernel symbols and debug kernels                               |

## 2. Background and symptom analysis

### 2.1 Observed guest environment (2026-11-23, collected with the same commands as `scripts/collect-env.sh`)

| Item              | Value                                                              |
| ----------------- | ------------------------------------------------------------------ |
| macOS             | 15.8.3 (build 24H241), Sequoia                                     |
| Architecture      | x86_64, CPU display name "Intel Core Processor (Skylake, IBRS, no TSX)" |
| SMBIOS model      | iMac19,1                                                           |
| Memory            | 32 GiB                                                             |
| SIP               | enabled, `csr-active-config` = `00000000`                          |
| boot-args         | `keepsyms=1`                                                       |
| Loaded third-party kexts | Lilu 1.6.8, WhateverGreen 1.6.7 (presumably injected by OpenCore — Q-01) |
| Xcode             | 26.3 (17C529), SDK `MacOSX26.2.sdk`                                |
| Kernel headers    | `IOKit/graphics/IOFramebuffer.h` and `IOKit/pci/IOPCIDevice.h` exist in the SDK `Kernel.framework` |
| KDK               | Not installed (no `/Library/Developer/KDKs`)                       |

### 2.2 Display device

| Item              | Value                                                               |
| ----------------- | ------------------------------------------------------------------- |
| PCI location      | 07:00.0 (under a PCIe root port, link x1)                          |
| Vendor / Device   | `0x1af4` / `0x1050` (virtio device type 16 = GPU, modern-only ID)   |
| Revision          | `0x01`                                                              |
| Class code        | `0x030000` (VGA compatible) → QEMU `virtio-vga`. It would be `0x038000` for `virtio-gpu-pci` |
| Subsystem         | `0x1af4` / `0x1100`                                                 |
| BAR0              | Memory `0x80000000`, 8 MiB — VGA/GOP framebuffer                    |
| BAR2              | Memory `0x81600000`, 16 KiB, prefetchable — presumably the virtio modern capability region |
| BAR4              | Memory `0x81000000`, 4 KiB — presumably the MSI-X table             |
| Expansion ROM     | `0x80800000`, 64 KiB                                                 |

The BAR interpretation comes from reading the ioreg `assigned-addresses` encoding. The actual role of each BAR (location of common/notify/isr/device cfg) is confirmed in Phase 1 by reading the PCI capability list.

### 2.3 Current driver state

- Services attached to the display device
  - `.Display_boot` — `IONDRVFramebuffer` (`com.apple.iokit.IONDRVSupport`), `IONameMatch "display"`, `IOProbeScore 20000`, `IOMatchCategory IOFramebuffer`
  - `AppleVirtIOPCITransport` (`com.apple.driver.AppleVirtIO` 230.100.3) — attaches to every `0x1af4` device. There is no client for the GPU, so it has no child services.
- Mode list: `IOFBConfig.IOFBModes` contains only one entry, ID 100 (1280×800).
- `IOFBMemorySize` = 4,096,000 = 1280 × 800 × 4 bytes.
- `system_profiler`: "VRAM (Total): 3 MB", "Kernel Extension Info: No Kext Loaded".
- `AppleParavirtGPU.kext` matches only `IOPCIMatch 0xEEEE106B` (a device specific to Apple Virtualization.framework), so it cannot be used for this device.

### 2.4 Causes

1. **VRAM shown as 3 MB**: This is not an actual video memory limit. The generic driver reports the GOP framebuffer size (4,096,000 bytes ≈ 3.9 MiB) as `IOFBMemorySize`, and this is rounded down in MB units, appearing as "3 MB".
2. **Fixed resolution**: `IONDRVFramebuffer` knows only the one mode set by the firmware. It has no means of handling mode switching, EDID, or host window size changes.
3. **Wallpaper and other display anomalies**: Measurements and the cause check are in §2.5. The guest has no Metal device (`MTLCopyAllDevices()` = empty list). The Sequoia wallpaper is an extension that draws with Metal, so it cannot be displayed and is replaced with solid Cyan. The opaque menu bar and Dock are presumed to have the same cause. **A 2D driver does not solve this** (R-03, Q-12).

### 2.5 Wallpaper symptom measurements (2026-11-23, Q-08)

Basis: the user capture (1280×800) was measured pixel by pixel, and settings were read with `osascript`, `defaults`, and `~/Library/Application Support/com.apple.wallpaper/Store/Index.plist`.

| Item                 | Measured value                                                                             |
| -------------------- | ------------------------------------------------------------------------------------------ |
| Configured wallpaper picture | `/System/Library/Desktop Pictures/Solid Colors/Cyan.png` (128×128 solid color, same on all desktops) |
| Area where the wallpaper is drawn | x 16–1263, y 40–783 — starting below the menu bar (24 px), inset 16 px on each side |
| Border               | 16 px on each side, color RGB(0,124,143) — a teal darker than the background color          |
| Inner background color | RGB(0,188,217). The difference from the original file's RGB(20,175,208) appears to be a color-management conversion (not confirmed) |
| Menu bar             | y 0–23, opaque solid RGB(218,218,218). The background color does not show through          |
| Dock background      | Opaque solid RGB(200,200,200)                                                               |
| Reduce transparency setting | No `com.apple.universalaccess reduceTransparency` key (the user has not turned it on) |

Interpretation:

- The wallpaper picture itself is drawn in the specified color. The anomalies are the **16 px border on each side** and **no transparency effects**.
- The menu bar and Dock are opaque even though the user has not turned on Reduce transparency. Presumably the system decided there is no GPU acceleration and turned off transparency and blur effects.
- The cause of the border was not confirmed. Candidates: the WindowServer/WallpaperAgent fallback path when there is no acceleration, or a layout problem coming from display information (no EDID or physical size). Check whether the border disappears after the 2D driver provides EDID and mode information.
- User report (2026-11-23): **A photo wallpaper was set in System Settings, but a solid color is displayed.** However, at this point the value recorded in the wallpaper store (`Index.plist`) was already `Solid Colors/Cyan.png`. That is, either the photo the user chose did not stay in the store, or the system replaced it with a solid color and saved that. Which one is the case was not confirmed.
- The display key in the wallpaper store is `FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF`. For a normal monitor it holds a UUID built from the EDID manufacturer, product, and serial number. This appears to mean there is currently no information to identify the display, and it may be related to wallpaper settings not being saved or applied (presumption).

#### Q-11 test (2026-11-23, performed with the user's permission)

The wallpaper was changed to `/System/Library/Desktop Pictures/Sonoma.heic` with `osascript`, captured with `screencapture` 4 seconds later, and then restored to the original store value `Solid Colors/Cyan.png`.

| Item       | Result                                                                                |
| ---------- | ------------------------------------------------------------------------------------- |
| Applied result | `get picture` returned `/System/Library/CoreServices/DefaultDesktop.heic` (the system recorded a different path) |
| Display    | The photo is drawn to the edge of the screen (x 0, y 799). **No 16 px border**         |
| Menu bar   | Opaque RGB(218,218,218) regardless of the wallpaper — still no transparency effects    |

Conclusion:

- When a photo file is specified directly, it is drawn correctly. So the "photo shown as a solid color" symptom is more likely **a problem in the process of saving and applying the chosen wallpaper** than a rendering failure. Candidates: ① no display identification information (UUID `FFFF…`) ② the wallpaper the user chose is a downloadable type (`.madesktop`, Aerial/dynamic wallpaper) that cannot be played without a GPU and is replaced with a solid color. Neither was confirmed.
- The 16 px border appeared only when showing the solid-color picture (Cyan.png). It may be the way solid-color wallpapers are displayed rather than a driver problem.
- The opaque menu bar and Dock appear regardless of the wallpaper type. This matches the presumption of missing GPU acceleration (R-03).

#### 2.5.2 Cause confirmed: the Sequoia wallpaper is drawn with Metal (2026-11-23)

User report: **Choosing the "Sequoia" wallpaper in System Settings shows solid Cyan.**

| Check item | Result |
| --------- | ---- |
| Sequoia wallpaper file | There is no Sequoia image file in `/System/Library/Desktop Pictures/`. The wallpaper is drawn in real time by `/System/Library/ExtensionKit/Extensions/WallpaperSequoiaExtension.appex` |
| Metal use by the extension | The Resources of `WallpaperSequoiaExtension.appex` (and `WallpaperMacintoshExtension.appex`) contain `default.metallib` (Metal shaders) |
| Metal device in the guest | `MTLCreateSystemDefaultDevice()` = `nil`, `MTLCopyAllDevices()` = `[]` — **no Metal device** |
| Store value | Sequoia was chosen, but `Index.plist` had `Solid Colors/Cyan.png` recorded |
| Comparison | A regular image file (`Sonoma.heic`) is displayed correctly without Metal (§2.5 Q-11 test) |

Conclusion:

- **A photo is shown as a solid color because there is no Metal device, so the Sequoia wallpaper extension cannot draw, and the system replaces it with solid Cyan.** The replacement itself was not seen in the logs (no related entries in the last 3 hours of logs). However, all four facts above are consistent with this explanation.
- This project's 2D framebuffer driver does not create a Metal device (§4.2 Out-of-Scope). **Therefore this driver does not solve the Sequoia wallpaper problem.** The menu bar and Dock transparency effects are also likely not to be solved for the same reason.
- Workaround available now: use a static image wallpaper (`.heic`, `.jpg`, `.png` — e.g. `Sonoma.heic`, `iMac *.heic`, a photo you add yourself). Extension-type wallpapers that use Metal (Sequoia, Macintosh, etc.) and dynamic or video wallpapers among the downloadable `.madesktop` ones may not be displayed.
- Metal support requires GPU driver interfaces that Apple has not published. It is work on the scale of a separate project and is not included in this scope (Q-12).

### 2.6 Mitigations possible without a driver (reference)

Separately from driver development, raising the GOP resolution with OpenCore `UEFI > Output > Resolution` or the OVMF resolution setting makes `IOFBMemorySize` grow proportionally. However, the GOP framebuffer must fit in BAR0 (currently 8 MiB), so at 32bpp the upper limit is 1920×1080 (7.9 MiB), and runtime resolution changes are still not possible. This project's driver uses guest RAM as backing, so it is not constrained by the BAR0 size.

## 3. Goals

| ID  | Goal                                                                         |
| --- | ---------------------------------------------------------------------------- |
| G-1 | Multiple resolutions can be selected within macOS and changed at runtime     |
| G-2 | Resolutions matching the host window size and EDID are provided automatically |
| G-3 | VRAM is shown as the size of the framebuffer memory the driver actually manages |
| G-4 | If the driver fails, the existing boot framebuffer is used again and the VM keeps booting |
| G-5 | Identify and document the cause of the wallpaper problem — done (§2.5.2: no Metal device). Record with AC-07 whether the result is the same after the driver is applied |

## 4. Scope

### 4.1 In-Scope

- IOKit kext for macOS 15 (Sequoia) x86_64 guests (C++, `Kernel.framework`)
- QEMU `virtio-vga` device (`1af4:1050`, class `0x030000`). As compatibility targets, `virtio-gpu-pci` (class `0x038000`) and `virtio-vga-gl` (same ID, works only through the 2D path without using 3D features — REQ-007)
- virtio 1.x modern PCI transport layer (own implementation on the guest side)
- virtio-gpu **2D** command set, EDID, hardware cursor
- `IOFramebuffer` subclass: mode list, mode switching, cursor, gamma (accept only), power state
- OpenCore injection-based install and rollback procedures and helper scripts
- User-space tool for status checks (IORegistry queries)
- Development environment: build, debugging (KDK, QEMU gdbstub), log collection procedures

### 4.2 Out-of-Scope

- 3D acceleration: virgl (the 3D features of `virtio-vga-gl`), Venus/Vulkan, Metal·OpenGL drivers, `IOAccelerator`. The `virtio-vga-gl` device itself is a 2D compatibility target
- Metal acceleration (including extension-type wallpapers such as Sequoia and transparency effects) is outside this scope. After the 2D driver is complete, its feasibility is investigated as a separate project (Q-12 finalized)
- Video decode/encode acceleration (unrelated to the `virtio-video` device type)
- Apple Silicon guests, Apple Virtualization.framework
- Modifying host (QEMU) code. On the host side, only changes to launch options are required (§8)
- DriverKit (dext) implementation: DriverKit has no display/framebuffer family
- Public distribution and Apple kext signing and notarization (Q-06 finalized: personal use. Public release on GitHub to be reconsidered after stabilization)

## 5. Functional requirements

Priorities use MoSCoW (M=Must, S=Should, C=Could, W=Won't this time).

### 5.1 Device matching and initialization

| ID      | Requirement                                                                                               | Priority |
| ------- | --------------------------------------------------------------------------------------------------------- | ---- |
| REQ-001 | Match with `IOPCIMatch 0x10501af4` using `IOPCIDevice` as the provider. Distinguish virtio-vga/virtio-gpu-pci by class code and record it | M |
| REQ-002 | Set the probe score so the driver takes precedence over `IONDRVFramebuffer` (score 20000) in `IOMatchCategory IOFramebuffer`, and verify that it actually replaces the boot framebuffer | M |
| REQ-003 | Confirm that there is no conflict with `AppleVirtIOPCITransport`, which attaches to the same device. If there is a conflict, record the workaround (matching adjustment, etc.) in the design document | M |
| REQ-004 | In `probe()`, check the device responses (virtio capability, feature, num_scanouts), and if the conditions are not met, reject the match so that `IONDRVFramebuffer` remains | M |
| REQ-005 | If any step of `start()` fails, release all allocated resources, reset the device, and return failure | M |
| REQ-006 | Declare dependencies such as `com.apple.iokit.IOGraphicsFamily` and `com.apple.iokit.IOPCIFamily` in `OSBundleLibraries` | M |
| REQ-007 | Work as a 2D driver on the `virtio-vga-gl` device as well. Do not negotiate `VIRTIO_GPU_F_VIRGL`. Whether the host virgl renderer processes 2D commands from a guest that has not negotiated 3D was not confirmed, so this is tested in Phase 2 (Q-13) | S |

### 5.2 virtio modern PCI transport layer

| ID      | Requirement                                                                                               | Priority |
| ------- | --------------------------------------------------------------------------------------------------------- | ---- |
| REQ-010 | Walk the PCI vendor capabilities (`cfg_type` 1–5) to find the BAR, offset, and length of the common/notify/ISR/device cfg regions. Reject if a range exceeds the BAR size | M |
| REQ-011 | Follow the initialization order in the specification: reset → ACKNOWLEDGE → DRIVER → feature negotiation → re-check FEATURES_OK → queue setup → DRIVER_OK | M |
| REQ-012 | Negotiate `VIRTIO_F_VERSION_1` as mandatory. Reject if it is absent                                     | M |
| REQ-013 | Implement split virtqueues. The descriptor table, avail ring, and used ring get DMA addresses through `IOBufferMemoryDescriptor` + `IODMACommand` | M |
| REQ-014 | Handle MSI-X interrupts with `IOFilterInterruptEventSource`/`IOInterruptEventSource`. The current device has 3 vectors (§8.1.1), so assign one each to config, controlq, and cursorq. If MSI-X cannot be used, operate with legacy INTx + ISR | M |
| REQ-015 | Provide a polling mode as a debug option so that operation can be checked without interrupts            | S |
| REQ-016 | `VIRTIO_F_RING_INDIRECT_DESC` and `VIRTIO_F_RING_EVENT_IDX` are offered by the current device (§8.1.1). Whether to use them is decided after performance measurement | C |
| REQ-017 | When the device reports `DEVICE_NEEDS_RESET`, reset the device and set the current mode again            | S |
| REQ-018 | Keep the transport layer in a class separate from virtio-gpu (so it can be reused for other virtio devices) | S |

### 5.3 virtio-gpu 2D commands

| ID      | Requirement                                                                                               | Priority |
| ------- | --------------------------------------------------------------------------------------------------------- | ---- |
| REQ-020 | Read the per-scanout enabled state and preferred resolution with `VIRTIO_GPU_CMD_GET_DISPLAY_INFO`        | M |
| REQ-021 | Implement `RESOURCE_CREATE_2D`, `RESOURCE_ATTACH_BACKING`, `SET_SCANOUT`, `TRANSFER_TO_HOST_2D`, `RESOURCE_FLUSH`, `RESOURCE_DETACH_BACKING`, `RESOURCE_UNREF` | M |
| REQ-022 | For the pixel format, use `VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM` (or the A8 variant), which matches macOS 32bpp (ARGB8888, byte order BGRA). Confirm on screen that colors are not swapped | M |
| REQ-023 | Check the response type of every command. On `RESP_ERR_*`, log it with the command name and arguments and return an error to the caller | M |
| REQ-024 | Serialize command submission and put a timeout on waiting for responses. Define the timeout value as a constant | M |
| REQ-025 | Negotiate `VIRTIO_GPU_F_EDID`, validate the EDID received with `GET_EDID` (length, checksum), and provide it to `IODisplay`. After applying, check whether the display key in the wallpaper store becomes a unique UUID rather than `FFFFFFFF-…` (§2.5) | S |
| REQ-026 | When `VIRTIO_GPU_EVENT_DISPLAY` is received in the device config `events_read`, re-read the display info and update the mode list | S |
| REQ-027 | Do not negotiate 3D/blob-related features such as `VIRTIO_GPU_F_VIRGL`, `VIRTIO_GPU_F_RESOURCE_BLOB`, `VIRTIO_GPU_F_CONTEXT_INIT` | M |

### 5.4 Framebuffer memory and screen updates

| ID      | Requirement                                                                                               | Priority |
| ------- | --------------------------------------------------------------------------------------------------------- | ---- |
| REQ-030 | Allocate the framebuffer from guest RAM (wired, page aligned) and attach it to the resource as a scatter-gather list. It does not need to be physically contiguous | M |
| REQ-031 | `getApertureRange()` returns the framebuffer memory of the current mode. WindowServer draws directly into this memory | M |
| REQ-032 | IOFramebuffer has no dirty-region notification, so send `TRANSFER_TO_HOST_2D` + `RESOURCE_FLUSH` periodically with a timer. Default period 60 Hz, configurable | M |
| REQ-033 | To reduce transfer volume when the screen does not change, add tile-based change detection (comparison or hash). The default is decided after measuring the effect | S |
| REQ-034 | On a mode switch, create a new resource, switch to it with `SET_SCANOUT`, then release the previous resource. There must be no kernel panic or hang during the switch | M |
| REQ-035 | When the driver takes over the screen, copy the boot framebuffer contents so the boot screen (Apple logo, progress bar) is not interrupted | S |
| REQ-036 | Make the maximum size of the framebuffer memory pool a setting. The default is 128 MiB (Q-03 finalized). It must not exceed the host `max_hostmem` (256 MiB) | M |

### 5.5 Display modes

| ID      | Requirement                                                                                               | Priority |
| ------- | --------------------------------------------------------------------------------------------------------- | ---- |
| REQ-040 | Provide a standard mode list: 1024×768, 1280×800, 1280×1024, 1440×900, 1600×900, 1680×1050, 1920×1080, 1920×1200, 2560×1440, 2560×1600. The maximum resolution is 2560×1600 (Q-02 finalized). Modes that do not fit in the memory pool are excluded | M |
| REQ-041 | Add the preferred resolution from display info and the modes from EDID to the list, and mark the preferred resolution as the default mode | S |
| REQ-042 | When the mode is changed in System Settings > Displays, the screen changes to that resolution (`setDisplayMode`) | M |
| REQ-043 | When the host window size changes (REQ-026), dynamically add a mode of that size and, depending on a setting, switch to it automatically | C |
| REQ-044 | Provide HiDPI (Retina) scaled modes (e.g. 2560×1600 as a 1280×800 UI)                                     | S |
| REQ-045 | Restore the last selected mode on the next boot (compatible with the way macOS saves display preferences) | S |
| REQ-046 | Support multiple scanouts (multiple monitors). Create as many `IOFramebuffer` instances as `num_scanouts` | C |

### 5.6 Cursor, gamma, power

| ID      | Requirement                                                                                               | Priority |
| ------- | --------------------------------------------------------------------------------------------------------- | ---- |
| REQ-050 | Implement `setCursorImage`/`setCursorState` with cursorq `UPDATE_CURSOR`/`MOVE_CURSOR` (64×64 ARGB)        | S |
| REQ-051 | If the hardware cursor cannot be used, operate with a software cursor (the driver reports that the hardware cursor is not supported) | M |
| REQ-052 | `setGammaTable` accepts and stores the values and returns success (virtio-gpu 2D has no gamma command)    | M |
| REQ-053 | Stop and resume the flush timer on display sleep (currently `displaysleep` 10 minutes) and wake. After waking, the lock (login) screen must be displayed correctly (§8.1.3) | M |
| REQ-054 | Re-initialize the device and restore the mode after system sleep/wake — excluded from this scope because Q-04 was finalized (system sleep not used). However, minimal handling that responds to power state change requests without panicking is kept within the scope of REQ-005·NFR-001 | W |

### 5.7 VRAM display and status reporting

| ID      | Requirement                                                                                               | Priority |
| ------- | --------------------------------------------------------------------------------------------------------- | ---- |
| REQ-060 | Report `IOFBMemorySize` as the framebuffer memory pool size                                               | M |
| REQ-061 | Investigate and set the registry properties needed so that "VRAM (Total)" in `system_profiler SPDisplaysDataType` shows the same value as REQ-060. Which property it reads has not been confirmed yet | M |
| REQ-062 | When the driver is applied, the "Kernel Extension Info: No Kext Loaded" line in `system_profiler SPDisplaysDataType` disappears, and the GPU name ("Chipset Model") is shown as `Virtio GPU` | S |
| REQ-063 | Expose the driver state (negotiated features, number of scanouts, current mode, queue statistics, error count) as IORegistry properties | S |
| REQ-064 | Log with the `os_log` family (or `IOLog`), and select the level (error/info/debug) with a boot-arg or an Info.plist setting | M |

### 5.8 Installation, rollback, tools

| ID      | Requirement                                                                                               | Priority |
| ------- | --------------------------------------------------------------------------------------------------------- | ---- |
| REQ-070 | Provide the install procedure as a document: set OpenCore `csr-active-config` (0x201), install into `/Library/Extensions`, rebuild the auxiliary kernel collection with `kmutil load -z`, reboot (Q-01 v1.2). The OpenCore injection procedure is kept for reference | M |
| REQ-071 | Provide the rollback procedure as a document: `-vvideooff`, boot into safe mode (`-x`) then delete the kext and rebuild with `kmutil`, Recovery `kmutil trigger-panic-medic`, restore SIP, restore the host disk snapshot | M |
| REQ-072 | Provide a boot-arg that disables the driver (e.g. `-vvideooff`). If this value is present, reject in `probe()` | M |
| REQ-073 | Document a `kmutil load`-based procedure for fast iteration during development — excluded from this scope because Q-01 was finalized (OpenCore injection only, SIP kept) | W |
| REQ-074 | Provide a status query CLI in `tools/` (reads IORegistry properties only, no user client)                 | C |
| REQ-075 | Collect the environment before and after applying the driver in the same format with `scripts/collect-env.sh` | M |

## 6. Non-functional requirements

| ID      | Category | Requirement                                                                                     |
| ------- | -------- | ----------------------------------------------------------------------------------------------- |
| NFR-001 | Stability | 0 kernel panics in 24 hours of continuous use (general desktop work, video playback)          |
| NFR-002 | Stability | 0 panics or screen freezes in 100 repeated mode switches, no memory leak (checked with `zprint`/IORegistry statistics) |
| NFR-003 | Stability | kext replacement is done by rebooting (reboot after `kmutil load -z -p`). Unloading while the driver owns the screen is not supported (v1.3, WI-5.003) |
| NFR-004 | Performance | Measure and record the increase in guest CPU usage with 1920×1080 60 Hz flushing. Target: 10% or less of one core (adjusted after measurement) |
| NFR-005 | Performance | Perceived latency when moving the mouse or dragging windows is no worse than now (IONDRV). Compared by recording |
| NFR-006 | Security | Range-check every value given by the host (response length, `num_scanouts` ≤ 16, EDID length ≤ 1024, resolution upper limit) |
| NFR-007 | Security | Do not create a user client. If one must be created, design entitlement checks and input validation together with it |
| NFR-008 | Compatibility | Target macOS 15.x, test baseline 15.8.3 (24H241). macOS 14 and earlier are not supported (Q-05 finalized) |
| NFR-009 | Compatibility | No conflict while Lilu/WhateverGreen are loaded. Check whether WhateverGreen touches this device |
| NFR-010 | Maintainability | Logic without kernel dependencies (mode calculation, EDID parsing, tile comparison, virtqueue index calculation) is verified with user-space unit tests |
| NFR-011 | Maintainability | No magic numbers. virtio/virtio-gpu constants are collected in headers with the specification section numbers as comments |
| NFR-012 | License | Do not copy GPL code (Linux virtio-gpu, QEMU). Use it only as a reference for understanding behavior and record the sources in the documents |
| NFR-013 | Observability | Keep `keepsyms=1` so the cause can be found on a panic, and log the main state transitions |

## 7. Technical constraints and design policy

1. **Only a kext is possible.** DriverKit has no family corresponding to `IOFramebuffer`. A third-party display driver is built as a kext that inherits `IOFramebuffer` from `IOGraphicsFamily`. `IOGraphicsFamily` (599) is currently loaded in the guest.
2. **Implement the transport layer ourselves (Q-07 finalized).** `AppleVirtIOTransport` is a private API without headers and may change with macOS updates. Use only the public API `IOPCIDevice`.
3. **Loading method**: Because SIP is enabled, an unsigned kext cannot be loaded with `kmutil load`. OpenCore injection can be used with SIP enabled. **Both development and installation use only OpenCore `Kernel > Add` injection (Q-01 finalized).** A reboot is needed for every test. SIP is not changed.
   - **Changed 2026-11-24 (v1.1)**: With T-04 confirmed, a framebuffer kext cannot be loaded by OC injection alone. Installation into `/Library/Extensions` (auxiliary kernel collection) with partial SIP relaxation (`CSR_ALLOW_UNTRUSTED_KEXTS` 0x1 + `CSR_ALLOW_UNAPPROVED_KEXTS` 0x200) is tested (Q-01).
4. **Build tools**: Whether Xcode 26.3 still has a kext project template was not confirmed. Check in Phase 0; if there is none, use a Makefile based on `xcrun clang -mkernel`. The target SDK is the installed `MacOSX26.2.sdk`, and the deployment target (`MACOSX_DEPLOYMENT_TARGET`) is 15.0.
5. **Debugging**: Source-level debugging with the QEMU gdbstub (`-s`) and the KDK for build 24H241. The user downloads the KDK from the Apple Developer site and installs it (sudo required).
6. **Screen update method**: virtio-vga stops VGA (BAR0) output once the virtio-gpu driver performs `SET_SCANOUT`. So a method that keeps using BAR0 cannot lift the resolution constraint (§2.6), and the guest RAM backing + periodic flush method is used.

## 8. Host (QEMU) requirements

This project does not modify the host configuration. The user sets up the following conditions.

### 8.1 Current host configuration (provided by the user 2026-11-23)

| Item          | Value                                                                               |
| ------------- | ----------------------------------------------------------------------------------- |
| QEMU          | 10.0.13                                                                             |
| `-device`     | `qemu-xhci,id=xhci`, `usb-kbd,bus=xhci.0`, `usb-tablet,bus-xhci.0` (see below), `usb-ehci,id=ehci`, `isa-applesmc`, `ich9-intel-hda`, `ich9-ahci`, `virtio-vga` |
| Display device | No `virtio-vga` properties specified → the defaults below are used as-is |
| Display backend | GTK (`-display gtk`). User report: changing to `-display gtk,gl=on` allows `virtio-vga-gl` to be used |

Interpretation:

- Because no properties are specified, the virtio-gpu defaults `xres=1280`, `yres=800` apply. **The guest's fixed 1280×800 resolution (§2.3) is caused by these defaults** (confirmed with the `help` output provided by the user).

#### 8.1.1 `virtio-vga` defaults relevant to the driver design (QEMU 10.0.13, `qemu-system-x86_64 -device virtio-vga,help`)

| Property         | Default         | Design impact                                                                               |
| ---------------- | --------------- | ------------------------------------------------------------------------------------------- |
| `xres` / `yres`  | 1280 / 800      | Preferred resolution reported by the device. Becomes the default mode of REQ-041            |
| `edid`           | on              | `VIRTIO_GPU_F_EDID` is offered. REQ-025 can be tested right away                            |
| `max_outputs`    | 1               | 1 scanout. Multiple monitors (REQ-046) cannot be tested without changing the host configuration |
| `max_hostmem`    | 268435456 (256 MiB) | Appears to be the upper limit of host-side resource memory (needs confirmation). The 128 MiB pool (REQ-036) and a 2560×1600 buffer (16 MiB) fit within it |
| `blob`           | off             | No blob resources. Consistent with REQ-027 (not negotiated)                                 |
| `disable-legacy` | auto            | Because it is in a PCIe slot, the legacy interface appears to be turned off. Only the modern transport is implemented (REQ-010–012) |
| `disable-modern` | off             | The modern interface can be used                                                             |
| `indirect_desc`  | on              | `VIRTIO_F_RING_INDIRECT_DESC` offered (REQ-016)                                             |
| `event_idx`      | on              | `VIRTIO_F_RING_EVENT_IDX` offered (REQ-016)                                                 |
| `packed`         | off             | No packed virtqueue. Only split virtqueues are implemented (REQ-013)                         |
| `vectors`        | 3               | 3 MSI-X vectors — can be assigned as 1 for config changes + 1 for controlq + 1 for cursorq (REQ-014) |
| `page-per-vq`    | off             | In the notify region, the address for each queue is calculated as `queue_notify_off × notify_off_multiplier` |
| `queue_reset`    | on              | `VIRTIO_F_RING_RESET` offered. Not used in this scope                                       |
| `ioeventfd`      | off             | Related to host performance. No effect on guest driver behavior                              |

#### 8.1.2 Assessment of `virtio-vga-gl` (`-display gtk,gl=on`)

- `virtio-vga-gl` is the device with the virgl 3D renderer enabled on the host side. For the guest to use 3D, there must be a **3D driver in the guest that produces virgl commands**. On Linux, Mesa (the virgl Gallium driver) plays that role, but macOS has no such driver. macOS apps use Metal, so a new Metal driver (private `IOGPUFamily` and Metal driver bundle interfaces) would have to be built and translate to virgl/Venus commands. The feasibility and size of this work were not confirmed, and it goes far beyond the scope of this 2D driver.
- Therefore **even after switching to `virtio-vga-gl`, this driver alone does not create a Metal device in the guest, and the Sequoia wallpaper problem (§2.5.2) remains.**
- Host `gl=on` uses the GPU for host display output, so it may help host-side display performance (not measured).
- The primary development and test device is the currently used `virtio-vga`, and `virtio-vga-gl` is kept as a compatibility test target (REQ-007) (Q-13 finalized).
- The current `virtio-vga` is a device without 3D, so it satisfies HST-001.
- The GTK backend notifies QEMU of the new display size when the window size changes. Whether this is delivered to the guest as `VIRTIO_GPU_EVENT_DISPLAY` is checked in Phases 1–3 (REQ-026, REQ-043).
- `usb-tablet,bus-xhci.0` in the provided list appears to be a typo for `bus=xhci.0`. It may be a typo made while copying it, and it is unrelated to this project.
- No host changes are needed right now for driver development. HST-002 (preferred resolution) and HST-005 (gdbstub) are added by the user when needed in the relevant Phase.


#### 8.1.3 Power and lock behavior (user answer on 2026-11-23, plus `pmset -g` and `sysadminctl -screenLock status`)

| Item                 | Value                                                                    |
| -------------------- | ------------------------------------------------------------------------ |
| System sleep         | User answer: not used. However, the `pmset` `sleep` value is 1 (minute) (R-09) |
| Display sleep        | `displaysleep 10` (10 minutes)                                           |
| Screen lock          | `screenLock delay is immediate` — locks immediately when the display turns off |
| User report          | Switches to the login screen after some time                             |

Interpretation: "login screen after some time" appears to be the behavior where the lock is applied immediately after display sleep (10 minutes). Therefore the driver must handle **display sleep/wake and the transition to the lock screen (REQ-053, AC-09)**, not system sleep (REQ-054, excluded).
### 8.2 Host conditions

| ID      | Requirement                                                                                       |
| ------- | ------------------------------------------------------------------------------------------------- |
| HST-001 | The primary test device is `-device virtio-vga`. `virtio-vga-gl` (requires `-display gtk,gl=on`) is used only for the REQ-007 compatibility test |
| HST-002 | `edid` defaults to on (confirmed). To change the preferred resolution, specify `xres=` and `yres=`. Example: `-device virtio-vga,xres=1920,yres=1080`. Before the driver is complete, you can test whether this setting alone changes the GOP resolution (§2.6, BAR0 8 MiB limit) |
| HST-003 | `max_outputs=N` only when testing multiple monitors                                               |
| HST-004 | Dynamic resolution (REQ-043) can be tested only with a display backend that sends window resize events. The current GTK backend is considered to meet this condition; actual event delivery is checked in Phase 3 |
| HST-005 | gdbstub (`-s` or `-gdb tcp::1234`) must be available for debugging                                |
| HST-006 | Create a guest disk snapshot before installing the driver (qcow2 `snapshot` or a file copy)       |

## 9. Verification and acceptance criteria

### 9.1 Acceptance criteria

| ID     | Criterion                                                                                        | Related requirements |
| ------ | ------------------------------------------------------------------------------------------------ | ----------------- |
| AC-01  | In `ioreg -c IOFramebuffer`, this driver's class is attached to the display device and there is no `IONDRVFramebuffer` | REQ-001, 002 |
| AC-02  | The VRAM in `system_profiler SPDisplaysDataType` is shown as the REQ-060 value                    | REQ-060, 061      |
| AC-03  | The REQ-040 mode list appears in System Settings > Displays, and switching to each mode actually changes the resolution (checked by screenshot size) | REQ-040, 042 |
| AC-04  | On the color check pattern (red, green, blue, white) screen, colors are not swapped               | REQ-022           |
| AC-05  | When the driver is disabled with a boot-arg, the system boots with the existing 1280×800 boot framebuffer | REQ-072, G-4 |
| AC-06  | Even when device initialization is forced to fail (debug option), booting continues and `IONDRVFramebuffer` remains | REQ-004, 005 |
| AC-07  | Compare and record before/after applying the driver: ① static photo wallpaper (Sonoma.heic) still displays correctly ② whether the Sequoia wallpaper is displayed (no fix expected, record only) ③ the 16 px border of solid-color wallpapers ④ menu bar and Dock transparency effects ⑤ the result of `MTLCopyAllDevices()` | G-5 |
| AC-08  | Pass the NFR-001 and NFR-002 tests and record the results                                         | NFR-001, 002      |
| AC-09  | Repeat display sleep → wake → lock screen → login 10 times with no screen corruption, black screen, or panic. The resolution is kept in every round | REQ-053, NFR-001 |

### 9.2 Records

- Keep the `scripts/collect-env.sh` output from before and after applying the driver, by date.
- Keep screen check results as screenshots and behavior recordings. Recordings include subtitles describing the current action.
- This project has no web UI, so it is not a Playwright E2E target. Instead, operations in System Settings > Displays are recorded with `screencapture -v` and subtitles are added (Q-09 finalized). Recording intervals follow the global rules: 1.5 seconds between screen transitions, 0.3–0.5 seconds between value inputs on the same screen.

## 10. Assumptions, constraints, risks

### 10.1 Assumptions

- A-1: The guest boots with OpenCore, and the user can edit `config.plist` and `Kexts/` on the EFI partition.
- A-2: The host QEMU is 10.0.13, and it was confirmed that the `edid` default is on. Support for `VIRTIO_GPU_EVENT_DISPLAY` does not appear in the `help` output and was not confirmed. It is confirmed in Phase 1 from the feature bits and config values read by the guest.
- A-3: Development and testing are done inside this VM, and if a problem occurs, it can be recovered from a disk snapshot on the host.

### 10.2 Risks

| ID   | Risk                                                                                   | Impact | Response                                                             |
| ---- | -------------------------------------------------------------------------------------- | ---- | -------------------------------------------------------------------- |
| R-01 | A kext defect causes a panic during boot → the VM cannot boot                          | High | REQ-072 boot-arg, separate OC boot entry, HST-006 snapshot           |
| R-02 | Conflict because `AppleVirtIOPCITransport` has already initialized and occupies the same device | High | Checked as the first task of Phase 1 (REQ-003)                 |
| R-03 | Extension-type wallpapers such as Sequoia and transparency/blur effects need Metal and are not solved by a 2D driver (absence of Metal confirmed in §2.5.2) | High | Inform the user that it is out of scope. Work around with a static image wallpaper. Metal support is out of scope; separate investigation after 2D is complete (Q-12 finalized) |
| R-04 | High CPU usage or screen tearing due to periodic flushing                              | Medium | REQ-033 change detection, period tuning, NFR-004 measurement       |
| R-05 | macOS updates change `IOGraphicsFamily` behavior or reduce support                     | Medium | Test on a fixed target build, snapshot before updates              |
| R-06 | WindowServer does not move from the boot framebuffer to the new driver (probe order/category problem) | High | Early in Phase 2, first check replacement with a minimal driver (REQ-002) |
| R-07 | A KDK matching build 24H241 cannot be obtained                                         | Low | Use a KDK for a nearby build, or log-based debugging                  |
| R-08 | The `system_profiler` VRAM display depends on private properties other than `IOFBMemorySize` | Low | Investigate under REQ-061; if not possible, adjust the acceptance criterion to be based on `IOFBMemorySize` (approval required) |
| R-09 | The `pmset` system sleep value is set to `sleep 1` (1 minute). The system may not have actually slept so far because of sleep-prevention requests from other processes. The user said sleep is not used, so if it actually sleeps, it falls into the excluded scope of REQ-054 | Medium | Recommend that the user turn off sleep with `sudo pmset -a sleep 0` (run by the user). The driver only responds to sleep requests without panicking |

## 11. Phase candidates

| Phase | Name                     | Main content                                                              | Related REQ               |
| ----- | ------------------------ | ------------------------------------------------------------------------- | ------------------------- |
| P0    | Development environment  | Finalize the build system, build and load an empty kext, OC injection and rollback procedures, KDK·gdbstub debugging | REQ-006, 070~072, 075     |
| P1    | virtio transport layer   | AppleVirtIO conflict check, capability parsing, initialization, virtqueue, interrupts | REQ-003, 010~018          |
| P2    | 2D single-mode output    | display info, resource·backing·scanout·flush, IOFramebuffer fixed mode, replacement check | REQ-001, 002, 004, 005, 007, 020~024, 027, 030~032, 034, 035 |
| P3    | Modes and VRAM           | Mode list, mode switching, EDID, display events, VRAM reporting           | REQ-025, 026, 036, 040~045, 060~064 |
| P4    | Cursor, power, optimization | Hardware cursor, gamma, sleep, change-detection flush                  | REQ-033, 050~054          |
| P5    | Stabilization and documentation | NFR tests, wallpaper comparison (AC-07), install document cleanup, tools | REQ-074, all NFRs      |
| P6    | Extensions (optional)    | Multiple monitors, automatic switching on dynamic resize                  | REQ-043, 046              |

## 12. Open Questions

Answer format example: `Q-01: A`, `Q-03: recommended` (`Q-03: 권장안`). Per the user instruction on 2026-11-23 ("Use the recommendations"), the multiple-choice questions were finalized with the recommended options. Items whose **Status** column is "Final" have been decided. Q-04·Q-08 are information requests rather than choices, so they remain open.

| ID   | Status | Question                                                                                 | Options                                                                                        | Recommendation |
| ---- | ---- | ---------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- | ------ |
| Q-01 | Final | How should the kext be loaded during development?                                       | A) OpenCore injection only (SIP kept, reboot every time) B) partial SIP relaxation + `kmutil load` (fast, weaker security) C) A by default, B only when needed | A. A reboot is needed, but SIP is kept and on failure the kext can be disabled immediately in OC. Also need to check whether Lilu/WhateverGreen are injected by OC | **Changed (2026-11-24, v1.1)**: it was confirmed that an IOFramebuffer subclass cannot be loaded by OC injection (T-04), so auxiliary kernel collection installation + partial SIP relaxation (0x201) is tested. Final decision depends on the test results | **Final (v1.2)**: 0.0.21 test succeeded |
| Q-02 | Final | What maximum resolution should be supported?                                            | A) 1920×1200 B) 2560×1600 C) 3840×2160                                                         | B. About 16 MiB per buffer, so the memory burden is small, and HiDPI 1280×800 can be supported |
| Q-03 | Final | What is the default size of the framebuffer memory pool (= reported VRAM)?              | A) current mode size only B) 64 MiB C) 128 MiB D) 256 MiB                                       | C. Two buffers of the maximum mode + headroom. 128 MiB wired out of 32 GiB of memory is a small burden |
| Q-04 | Final | Host launch information: device options in the OSX-KVM launch script, QEMU version, display backend (GTK/SPICE/VNC), whether VM sleep is used | Share the launch script contents | Received: QEMU 10.0.13, `-device` list (§8.1), `virtio-vga` defaults (§8.1.1), GTK backend (§8.1.2), sleep not used and login screen after some time (§8.1.3) |
| Q-05 | Final | Should macOS 14 and earlier also be supported?                                          | A) 15 only B) 14 and 15 C) 13 and later                                                         | A. The current VM is 15 and there is one test environment. Extend when needed |
| Q-06 | Final | What is the distribution scope?                                                          | A) personal use B) public on GitHub (build artifacts, unsigned, assumes OC injection) C) public with Apple kext signing | Start with A, consider B after stabilization. C requires an Apple kext signing certificate |
| Q-07 | Final | Implement the virtio transport layer ourselves, or attach to Apple `AppleVirtIOTransport`? | A) own implementation (public API only) B) use Apple private API                              | A. The private API has no headers and may break with updates |
| Q-08 | Answered | What exactly does "the wallpaper and so on are not displayed correctly" look like? | black background / does not change / no blur·transparency effects / corruption, etc. | Answered with the user capture and report. A photo was set but a solid color is shown, a 16 px border when a solid color is shown, opaque menu bar and Dock (§2.5) |
| Q-09 | Final | Verification recording method                                                            | A) record System Settings operations with `screencapture -v` + subtitles B) screenshots only   | A. Apply the global rule (screenshots, video, subtitles) in a way suited to a driver project |
| Q-10 | Final | License                                                                                  | A) MIT B) BSD-2-Clause C) Apache-2.0 D) undecided (private)                                     | Finalized as A (MIT). The recommendation was "A or B", so A, which comes first in the option order, was chosen. No GPL code is used, so a permissive license can be used |
| Q-11 | Answered | Does the same symptom occur with a photo wallpaper? | A) briefly switch to a photo wallpaper and capture B) use only the solid-color baseline | Tested with A. A static photo (Sonoma.heic) is fine; the wallpaper the user chose was Sequoia (Metal extension type), replaced with Cyan (§2.5.2) |
| Q-12 | Final | Should Metal acceleration be added to the goals for the Sequoia wallpaper and transparency effects? | A) keep it out of this scope (2D driver first) B) investigate feasibility as a separate project after 2D is complete C) consider methods outside the driver, such as host GPU passthrough D) develop a Metal driver on top of `virtio-vga-gl` + virgl | **Final: A + B.** D would require building a new Metal driver because there is no virgl 3D driver for macOS (§8.1.2), and with private interfaces, even its feasibility is uncertain |
| Q-13 | Final | What should be the primary development and test device? | A) `virtio-vga` primary, `virtio-vga-gl` for compatibility testing (REQ-007) B) `virtio-vga-gl` primary C) `virtio-vga` only | **Final: A.** Development can proceed with the current VM configuration as-is, and the 2D driver does not use the 3D features of the gl variant |

## 13. References

| Source                                                       | Purpose                                  |
| ------------------------------------------------------------ | ---------------------------------------- |
| OASIS *Virtual I/O Device (VIRTIO) Version 1.2/1.3* — §4.1 PCI transport, §2.7 split virtqueue, §5.7 GPU Device | Reference specification for the transport layer and GPU commands |
| Apple `Kernel.framework` headers: `IOFramebuffer.h`, `IOGraphicsTypes.h`, `IOPCIDevice.h` (installed SDK) | IOKit API reference |
| Apple IOGraphics open source (apple-oss-distributions/IOGraphics) | Reference for `IONDRVFramebuffer` behavior and mode handling |
| QEMU `hw/display/virtio-gpu*.c`, `virtio-vga.c` (GPL — for understanding behavior only, do not copy) | Checking host-side behavior and BAR layout |
| OpenCore Configuration.pdf — `Kernel > Add`, `UEFI > Output`  | Install and rollback procedures          |
