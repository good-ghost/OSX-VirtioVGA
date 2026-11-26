# Installation, Settings and Rollback Procedures

English | [한국어](INSTALL.md)

Target environment: OSX-KVM guest macOS 15.x (verified: 15.8.3 `24H241`), OpenCore 1.0.6, EFI partition `disk0s1`, QEMU `-device virtio-vga`.
Rewritten as of 2026-11-25 (WI-5.009). When the configuration changes, this document is updated as well.

The kext runs in the kernel. Do the preparation in §1 first every time you install or update.

**When installing on a new VM, follow [NEW-VM-INSTALL.en.md](NEW-VM-INSTALL.en.md) (pkg).** This document covers the development procedure of building from source and installing/replacing individual files by hand, along with details of settings and rollback. The names changed in 0.9.0 (`VirtioVideo` → `VirtioVGA`, bundle id `local.virtiovideo.*` → `local.virtiovga.*`).

## Components at a glance

| Component | Location | Loading method | Role |
| --------- | ---- | --------- | ---- |
| `VirtioVGA.kext` (main driver) | `/Library/Extensions` | Auxiliary kernel collection (`kmutil load -z -p`, then reboot) | virtio-gpu device initialization, `IOFramebuffer`, screen transfer, cursor, EDID |
| `VirtioVGANdrvBlocker.kext` (helper) | `EFI/OC/Kexts` | OpenCore injection (Boot KC) | Adds `AAPL,iokit-ignore-ndrv` to the GPU PCI device early in boot so that IONDRV does not attach |
| `vvresize` | pkg: `/Library/Application Support/VirtioVGA` + `/Library/LaunchAgents`, development: `~/Library/Application Support/VirtioVGA` (`scripts/vvresize-agent.sh install`) | Starts at login | Completes the automatic host window-size switching (§5.4). Keep only one of the two |
| Tools (pkg) | `/Library/Application Support/VirtioVGA` | — | `opencore-setup.sh` (OpenCore setup), `uninstall.sh` (removal), `vvstat` (status query) |

- Why the main driver is not injected through OpenCore: a kext that uses IOGraphicsFamily symbols does not load through OpenCore injection (T-04).
- SIP: `csr-active-config` = `01020000` (0x201, only Kext Signing disabled). Do not relax it further than this.
- Replace the kext **by rebooting**. Running `kmutil unload` while the driver is in charge of the screen leaves the VM stopped in a panic state (NFR-003 v1.3, WI-5.003).

## 1. Preparation (every time you install or update)

1. Create a **host disk snapshot**. Run it with the VM shut down (running `qemu-img snapshot` on a qcow2 while the VM is running can corrupt the image).

   ```bash
   qemu-img snapshot -c before-virtiovga-<date> <guest disk image path>.qcow2
   ```

   Listing and restoring:

   ```bash
   qemu-img snapshot -l <guest disk image path>.qcow2
   ```

   ```bash
   qemu-img snapshot -a before-virtiovga-<date> <guest disk image path>.qcow2
   ```

   If the guest disk is not qcow2, make a copy of the image file.

2. Build and test: `scripts/build.sh`, `scripts/test.sh` (both exit code 0), `kmutil libraries -p build/VirtioVGA.kext` (exit code 0).
3. Check that the dSYM of the build to be installed is in `build/dsym-archive/<UUID>.dSYM` (needed for panic analysis, [DEBUGGING.en.md §2.3](DEBUGGING.en.md)). UUID: `dwarfdump --uuid build/VirtioVGA.kext/Contents/MacOS/VirtioVGA`

## 2. One-time setup (OpenCore)

Mount the EFI (administrator password, entered by the user). The mount point is `/Volumes/EFI`; it is unmounted on reboot.

```bash
sudo diskutil mount disk0s1
```

Back up before making changes (record it in the §9 list).

```bash
sudo cp -p /Volumes/EFI/EFI/OC/config.plist /Volumes/EFI/EFI/OC/config.plist.bak-<date>-<reason>
```

`packaging/opencore-setup.sh` (in the pkg, `/Library/Application Support/VirtioVGA/opencore-setup.sh`) makes the settings below and copies the helper kext in one go after a backup. When doing it by hand, `config.plist` must contain the following.

| Location | Value |
| ---- | -- |
| `NVRAM > Add > 7C436110-AB2A-4BBB-A880-FE41995C9F82 > csr-active-config` | `01020000` (data) |
| `NVRAM > Delete > 7C436110-…` | includes `csr-active-config`, `boot-args` |
| `NVRAM > Add > 7C436110-… > boot-args` | `keepsyms=1` (default) |
| `Kernel > Add` VirtioVGANdrvBlocker | `Arch x86_64`, `BundlePath VirtioVGANdrvBlocker.kext`, `ExecutablePath Contents/MacOS/VirtioVGANdrvBlocker`, `PlistPath Contents/Info.plist`, `MinKernel 24.0.0`, `Enabled true`. After Lilu and WhateverGreen |
| `Kernel > Add` VirtioVGA (old OpenCore injection entry) | If present, `Enabled false` (since 2026-11-24) |

- If a data value is entered with PlistBuddy, it is stored as an ASCII string. Use `plutil -replace … -data AQIAAA==` (= `01020000`).
- To add an item to an array, use `plutil -insert Kernel.Add -json '{…}' -append` instead of PlistBuddy (adding with PlistBuddy put it in the wrong position).
- After editing, `plutil -lint /Volumes/EFI/EFI/OC/config.plist` must print `OK`.

Copy the helper kext (the first time, and whenever the helper kext changes):

```bash
sudo rm -rf /Volumes/EFI/EFI/OC/Kexts/VirtioVGANdrvBlocker.kext && sudo ditto --norsrc --noextattr build/VirtioVGANdrvBlocker.kext /Volumes/EFI/EFI/OC/Kexts/VirtioVGANdrvBlocker.kext && sudo dot_clean -m /Volumes/EFI/EFI/OC
```

After rebooting, check that `csrutil status` is Custom Configuration (Kext Signing: disabled).

## 3. Installing and updating the main driver

The user enters the password in the terminal.

```bash
sudo rm -rf /Library/Extensions/VirtioVGA.kext && sudo ditto --norsrc --noextattr build/VirtioVGA.kext /Library/Extensions/VirtioVGA.kext && sudo chown -R root:wheel /Library/Extensions/VirtioVGA.kext && sudo kmutil load -z -p /Library/Extensions/VirtioVGA.kext
```

- The normal result is Code 28 "requires a reboot". Then reboot.
- Without `-z` (skip the approval check) it fails with Code 27 (not approved). The unsigned kext could not be approved with the Allow button in System Settings.
- Check before rebooting (optional): `kmutil inspect -B /Library/KernelCollections/AuxiliaryKernelExtensions.kc | grep virtiovga`

## 4. Verifying the installation

```bash
scripts/vvstat.sh
```

- Exit code 0 means the driver initialized the device (`init driver-ok`) and the framebuffer is attached.
- The first line shows the kext version; `--log 20` shows recent logs. For a detailed interpretation see [DEBUGGING.en.md §1](DEBUGGING.en.md).
- Load location: `kmutil showloaded --collection aux | grep -i virtiovga` (main driver), `kmutil showloaded --collection boot | grep -i virtiovga` (helper kext)
- IONDRV blocking: `ioreg -r -c IOPCIDevice -l -w0 | grep VVNdrvBlocked` → `"set"`

## 5. Settings

### 5.1 boot-args

OpenCore **rewrites boot-args from the config.plist value on every boot** (`boot-args` is in `NVRAM > Delete`). Changing them with `nvram boot-args=...` in macOS or Recovery is overwritten on the next boot, so edit config.plist.

```bash
sudo /usr/libexec/PlistBuddy -c "Set :NVRAM:Add:7C436110-AB2A-4BBB-A880-FE41995C9F82:boot-args keepsyms=1 <value to add>" /Volumes/EFI/EFI/OC/config.plist
```

| boot-arg | Value | Default | Purpose |
| -------- | -- | ---- | ---- |
| `-vvideooff` | flag | none | Disables the driver. The helper kext does not add the property and the main kext also refuses matching, so IONDRV takes the screen (§6) |
| `-vvideoswcursor` | flag | none | Turns off the hardware cursor and uses a software cursor (REQ-051) |
| `vvideo_cursor_scale` | 50 / 100 | Info.plist value | Cursor size ratio (§5.2) |
| `vvideo_flush_ms` | 4–1000 | 16 | Screen transfer period (ms). Out-of-range values fall back to the default |
| `vvideo_dirty` | 0 / 1 | Info.plist value | Transfer only changed regions (§5.2) |
| `vvideo_dirty_tile` | power of 2 from 16 to 256 | Info.plist value | Change detection tile size |
| `vvideo_autoresize` | 0 / 1 | Info.plist value | Automatic host window-size switching (§5.4) |
| `vvideo_log` | 0 / 1 / 2 | 1 | Log level (0 error, 1 info, 2 debug) |
| `vvideo_irq` | 0 / 1 / 2 | 0 | Interrupt mode (MSI-X / INTx / Poll). On this device, 1 falls back to Poll |
| `vvideo_fail` | 1 / 2 / 3 | none | For testing: injects a failure at the transport layer, device initialization, or framebuffer stage (to check that it falls back to IONDRV) |
| `vvideo_test_reset` | 1 | none | For testing: simulates a device reset after 30 seconds |
| `vvideo_test_pattern` | 1 | none | For testing: color bands for 15 seconds after 90 seconds |
| `vvideo_takeover` | 1 | none | For testing: takes over from IONDRV while running. **May cause a panic** |

### 5.2 kext Info.plist defaults

These are defaults set by installing the kext alone, without editing the EFI. They are in `IOKitPersonalities > VirtioGpuDevice` in `kext/Info.plist`. If you change them, rebuild and reinstall. If a boot-arg with the same name exists, the boot-arg wins.

| Key | Default | Meaning |
| -- | ---- | ---- |
| `VVCursorScale` | 100 | Hardware cursor size (%). QEMU GTK draws the cursor without the window scale factor. When a HiDPI guest (2560×1600) is shown at half size, such as in a 1280×800 host window, set 50 (boot-arg `vvideo_cursor_scale=50`). Default 100 since 0.9.1 (WI-4.001) |
| `VVDirtyFlush` | 0 | Transfer only changed regions. Measurements showed no gain on the host and only increased guest CPU, so it is off |
| `VVDirtyTile` | 64 | Change detection tile size |
| `VVAutoResize` | 1 | Automatic host window-size switching (§5.4). With 0, the mode list is not changed even when the window size changes |

### 5.3 Changing at runtime (for measurement)

Only `VVDirtyFlush` and `VVDirtyTile` can be changed without a reboot. Root is required.

```bash
scripts/vvctl.sh dirty 1
```

### 5.4 Automatic host window-size switching (WI-6.001)

When you resize the QEMU window on the host, the guest resolution changes to that size. It consists of two parts: the driver and the agent.

1. Driver: when it receives a window-size event, it adds that size as a dynamic mode (ID 100), and when events stop for 600 ms it notifies IOGraphics of a connection change. WindowServer rereads the mode list and switches to ID 100, the EDID preferred resolution (= the window size).
2. Agent (`vvresize`): WindowServer then returns to the mode saved in the display settings. If the saved mode is a standard mode (for example 1920×1080 chosen in System Settings), the agent switches back to ID 100 and saves it in the settings. Once saved, subsequent changes are made by WindowServer alone.

Install, check, remove (no sudo needed, log `~/Library/Logs/vvresize.log`):

```bash
scripts/vvresize-agent.sh install
```

```bash
scripts/vvresize-agent.sh status
```

```bash
scripts/vvresize-agent.sh uninstall
```

- Turning it off: boot-arg `vvideo_autoresize=0` or Info.plist `VVAutoResize` 0. The agent follows the driver setting and does nothing.
- The screen flickers once when the window is resized. It takes about 1 second for the resolution to change (about 3 seconds and two flickers if the saved mode is a standard mode).
- The agent saves the mode it switched to in the display settings. If you choose a resolution in System Settings, that value is saved again.
- Status: the `auto resize` line of `scripts/vvstat.sh` (`on handler=enabled connect-changes=N last=WxH`).

## 6. How to disable or remove the driver

Use the lightest method first. If you installed with the pkg, complete removal is done with `sudo "/Library/Application Support/VirtioVGA/uninstall.sh"` (it disables the helper kext first, then deletes the main kext). If only the helper kext remains and the main kext is gone, IONDRV is also blocked and no screen appears.

1. **boot-arg `-vvideooff`** (§5.1): the kext loads but does not attach to the device. Check: `scripts/vvstat.sh` shows the framebuffer as `not attached` or `VirtioGpuDevice: not found`.
2. **Disable the helper kext**: set `Enabled false` for VirtioVGANdrvBlocker in `Kernel > Add`. The main kext does not match as a framebuffer if IONDRV has attached first.
3. **Remove the main kext**: `sudo rm -rf /Library/Extensions/VirtioVGA.kext`, then `sudo kmutil rebuild`, then reboot. (This procedure has not been run yet.)

## 7. When the system does not boot

If it boots but the screen looks wrong, see §7.3; if it does not boot at all, start from §7.1.

### 7.1 Booting into Recovery from the OpenCore boot picker

Current settings: `ShowPicker=true`, `Timeout=2` (seconds), `HideAuxiliary=true`, `PickerMode=External`.

1. Start the VM and, within 2 seconds, press an arrow key in the boot picker to stop the automatic boot.
2. Because `HideAuxiliary=true`, Recovery is hidden. Press **Space** to show the auxiliary entries and choose macOS Recovery.
3. In Recovery, Utilities > Terminal (the Recovery terminal runs as root, so no password is needed):

   ```bash
   diskutil mount disk0s1
   ```

   ```bash
   ls /Volumes/EFI/EFI/OC/config.plist.bak-*
   ```

   ```bash
   cp /Volumes/EFI/EFI/OC/config.plist.bak-<most recent good backup> /Volumes/EFI/EFI/OC/config.plist
   ```

4. Reboot.

Instead of reverting to a backup, you can also add `-vvideooff` to boot-args in the same terminal (the PlistBuddy command in §5.1, without `sudo` in Recovery).

If the main kext itself has to be removed: enter Safe Mode (boot-arg `-x` or Shift at boot) and run `sudo rm -rf /Library/Extensions/VirtioVGA.kext`, then `sudo kmutil rebuild`; if that still does not work, run `kmutil trigger-panic-medic --volume-root /Volumes/<volume>` in Recovery. (Neither has been run yet.)

OpenCore injects the helper kext (NdrvBlocker) in Recovery as well. If even Recovery does not boot, go to §7.2.

### 7.2 Restoring the host snapshot

Shut down the VM and revert with `qemu-img snapshot -a` from §1. The EFI partition is on the same disk (`disk0s1`), so it is reverted as well.

### 7.3 When it boots but the screen looks wrong (recovery over SSH from the host)

macOS keeps running, so add `-vvideooff` through Remote Login (SSH). Remote Login must be enabled on the guest (enabled as of 2026-11-24, guest IP `192.168.122.75`).

```bash
ssh woomg@192.168.122.75
```

In the guest shell (the user enters the password):

```bash
sudo diskutil mount disk0s1
```

```bash
sudo /usr/libexec/PlistBuddy -c "Set :NVRAM:Add:7C436110-AB2A-4BBB-A880-FE41995C9F82:boot-args keepsyms=1 -vvideooff" /Volumes/EFI/EFI/OC/config.plist && plutil -lint /Volumes/EFI/EFI/OC/config.plist
```

```bash
sudo shutdown -r now
```

Even if the screen freezes, the Claude desktop session keeps running inside the guest. You can continue diagnosis from another device with Remote Control.

## 8. Known limitations

| Item | Details |
| ---- | ---- |
| Unloading at runtime | Not supported. Running `kmutil unload` while the driver is in charge of the screen leaves the VM stopped in a panic state (WI-5.003) |
| System sleep | A setting that disables it (`pmset -a sleep 0`) is recommended. A QEMU VM does not wake from S3 on key input. Going to sleep works without a panic, but waking could not be tested (WI-4.003) |
| Display sleep | Supported. During sleep the host window shows "Display output is not active." and it returns on key input (WI-4.002) |
| Metal and transparency effects | Not available. As a 2D framebuffer driver, no Metal device is created. Metal wallpapers such as Sequoia are replaced with a solid color, and the menu bar and Dock are opaque (Q-12, WI-5.007) |
| Recording | Guest `screencapture -V` fails. Record the screen on the host |
| Window minimize animation | At 2560×1600 HiDPI, minimize (Genie) takes 3–10 seconds and looks choppy. This is because there is no Metal, so WindowServer draws with the CPU. At 1280×800 1:1 it takes 1–3 seconds, similar to IONDRV. Workaround: a 1:1 mode or the Dock minimize effect "Scale" |

## 9. config.plist backup list

| File (EFI/OC/) | When created | Contents |
| ------------------------------------- | --------------------------------- | ------------------------------------------ |
| `config.plist.bak-20261123-wi0002` | Before the first WI-0.002 install | Original settings without a VirtioVGA entry |
| `config.plist.bak-20261123-wi0005` | Before the WI-0.005 boot-args change | VirtioVGA entry present, boot-args `keepsyms=1` |
| `config.plist.bak-20261123-wi0003` | Before the WI-0.003 test | VirtioVGA entry present (Enabled=true), boot-args `keepsyms=1 vvideo_log=0` |
| `config.plist.bak-20261123-wi0004` | Before re-enabling the kext in WI-0.004 | VirtioVGA entry present (Enabled=false), boot-args `keepsyms=1` |
| `config.plist.bak-20261124-wi1001` | Before the WI-1.001 test | Enabled=true, boot-args `keepsyms=1` |
| `config.plist.bak-20261124-wi1007` | Before the WI-1.007 Poll test | Enabled=true, boot-args `keepsyms=1` |
| `config.plist.bak-20261124-wi1006` | Before the WI-1.006 recovery test | Enabled=true, boot-args `keepsyms=1 vvideo_irq=2` |
| `config.plist.bak-20261124-wi2004` | Before the WI-2.004 pattern test | Enabled=true, boot-args `keepsyms=1` |
| `config.plist.bak-20261124-wi2005-t04` | Before the T-04 stub (0.0.21) OC injection test | Enabled=true, boot-args `keepsyms=1` |
| `config.plist.bak-20261124-auxkc-sip` | Before SIP 0x201 and turning off OC injection | Enabled=true, csr `00000000` |
| `config.plist.bak-20261124-wi2004-redo` | Before reinstalling WI-2.004 after the snapshot restore | Original settings without a VirtioVGA entry (the backups above disappeared from the EFI because of the restore) |
| `config.plist.bak-20261125-wi4001` | Before the WI-4.001 software cursor test | boot-args `keepsyms=1` |
| `config.plist.bak-20261125-minimize` | Before the minimize comparison with IONDRV | boot-args `keepsyms=1` |
| `config.plist.bak-20261125-wi5010` | Before the WI-5.010 AC-05/06 reboot tests | boot-args `keepsyms=1` |
| `config.plist.bak-20261125-wi6001` | Before the WI-6.001 test of turning off automatic switching | boot-args `keepsyms=1` |
| `config.plist.bak-20261125-200809-virtiovga` | Before the 0.9.0 rename and the first `opencore-setup.sh` run | Old VirtioVideo and VirtioVideoNdrvBlocker entries present |
| `config.plist.bak-20261125-201204-virtiovga` | Before re-running `opencore-setup.sh` (upgrade test) | VirtioVGANdrvBlocker entry present |

All backups in the table above except `wi2004-redo` disappeared from the EFI when the 2026-11-24 host snapshot "WI-001" was restored.
