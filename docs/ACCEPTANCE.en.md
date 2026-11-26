# Final Acceptance Criteria Check (WI-5.010, M3)

English | [한국어](ACCEPTANCE.md)

Check date: 2026-11-25. Target: kext 0.0.42 (`local.virtiovga.VirtioVGA`), helper kext `VirtioVGANdrvBlocker`. Environment: macOS 15.8.3 guest, QEMU 10.0.13 `-device virtio-vga` (GTK), 4 vCPUs, boot-args `keepsyms=1`.
The criteria are in [REQUIREMENT.en.md §9.1](REQUIREMENT.en.md).

## Results

| ID | Criterion (summary) | Result | Method and evidence (whether re-checked on 0.0.42) |
| -- | ----------- | ---- | ----------------------------------- |
| AC-01 | This driver attaches to the display and there is no `IONDRVFramebuffer` | Pass | Re-checked. `ioreg -c IOFramebuffer` shows one `VirtioVideoFramebuffer`, with `display0` and `AppleDisplay` under it. 0 `IONDRVFramebuffer` |
| AC-02 | VRAM is shown with the REQ-060 value | Pass | Re-checked. `system_profiler`: VRAM (Total) 128 MB, Vendor 0x1af4, Device 0x1050 |
| AC-03 | The REQ-040 modes are visible, and changing them changes the actual resolution | Pass | Re-checked. `scripts/ac03-modes.swift`: switched through the 10 modes in turn; screen capture size = mode pixel size 10/10, and the driver scanout changed to the same size (`setDisplayMode` log). Restored to the original mode |
| AC-04 | Red, green, blue and white are not swapped | Pass | Re-checked. Displayed color bands full screen in the guest with `scripts/color-bands.swift`; the user confirmed the correct order in the host QEMU window. 0 flush and device errors during display |
| AC-05 | When disabled with a boot-arg, the system boots with the existing 1280×800 boot framebuffer | Pass | Re-checked. `-vvideooff` boot: 1 `IONDRVFramebuffer`, 0 of our framebuffers, `VVNdrvBlocked = skipped:-vvideooff`, 1280×800, VRAM 3 MB, 0 panics |
| AC-06 | Even when initialization is forced to fail, boot continues and IONDRV remains | Pass | Re-checked (device initialization stage). `vvideo_fail=2` boot: log `init failed: injected:bring-up` → `handing the screen back to IONDRV`, 1 `IONDRVFramebuffer`, 1280×800, 0 panics. The transport layer (1) and framebuffer (3) stages are 0.0.25 results (WI-2.008) |
| AC-07 | Before/after comparison record for wallpaper, transparency effects and Metal | Pass (recorded) | Recorded on 0.0.42 (WI-5.007): ① photo OK ② Sequoia still not drawn (replaced with white) ③ solid-color 16 px border gone ④ menu bar and Dock opaque ⑤ 0 Metal devices. ②④⑤ are out of scope (Q-12) |
| AC-08 | NFR-001 (continuous use) and NFR-002 (100 mode switches) pass | Pass (with limitations) | NFR-002 re-checked: 100/100 ok, 0 panics, 0 errors, driver object count the same before and after. NFR-001: 0.0.42 for 12 hours (shortened from the planned 24 hours by user decision), 0 panics, 0 errors, no object growth. The screen was on and in use for about 43 minutes; the rest was display sleep — the user accepted this together with its limitations |
| AC-09 | Display sleep → wake → lock → login 10 times, no corruption, black screen or panic, resolution kept | Pass | Re-checked. `scripts/display-sleep-cycles.sh 10`: 10/10 ok, capture 2560×1600 after login in every round, 0 flush errors, 0 panics |

## Verdict

AC-01 to AC-09 are all considered passed. AC-08 is a result in which the user accepted that the NFR-001 test conditions (24 hours, normal use) were not fully met.

## Remaining items

- **Demo recording**: cancelled (user decision 2026-11-25). Guest `screencapture -V` failed, so host recording would have been needed. The recording item of WI-2.007 was also cancelled by the same decision.
- **AC-03 screen check**: the System Settings > Displays screen could not be captured this time (the guest has no screen recording permission, so other apps' windows do not appear in captures). The mode list was checked with the CoreGraphics list that System Settings uses, and switching from the settings screen was confirmed by the user on 0.0.26.
- **How the mode list is shown**: macOS shows only one mode per pixel size. 1440×900, 1600×900, 1680×1050, 1920×1080, 1920×1200, 2560×1440 and 2560×1600 appear as HiDPI (half-size UI). All 10 pixel sizes of REQ-040 are present. During WI-5.002, the same API showed 17 (both 1:1 and HiDPI). The cause of the difference was not identified.
- **REQ-062(S)**: the wording changed in v1.4 ("No Kext Loaded" disappears and the GPU name is shown). Met in 0.0.51: the `system_profiler` title and "Chipset Model" are `Virtio GPU`, and there is no "No Kext Loaded".
- **Known limitations**: the window minimize animation is slow in HiDPI (no Metal). The rest are in [INSTALL.en.md §8](INSTALL.en.md).
- **Wired memory**: about +299 MiB in the 12-hour test and about +39 MiB (10,048 pages) in 100 mode switches. The driver object count did not change, so this is not considered a driver leak, but the cause was not identified.
