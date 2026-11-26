# Installing on a New VM (pkg)

English | [한국어](NEW-VM-INSTALL.md)

Starting with VirtioVGA 0.9.0, installation is done with an installer package (`VirtioVGA-<version>.pkg`). This document describes a VM on which the driver is being installed for the first time. How to build from source or replace individual files by hand is in [INSTALL.en.md](INSTALL.en.md).

## 1. Requirements and conditions

| Item | Condition |
| ---- | ---- |
| Guest | macOS 15.x, Intel (x86_64). Verified version: 15.8.3 `24H241` |
| Host | QEMU/KVM (OSX-KVM). The display device must be `-device virtio-vga` (or `virtio-vga-gl`) |
| Boot loader | OpenCore. Verified version: 1.0.6. `Disabled` is recommended for `Misc > Security > SecureBootModel` |
| Privileges | An administrator account (sudo) on the guest |
| Way to roll back | A disk snapshot made on the host (step 2 below) |

- If the display device is not in the `virtio-vga` family (for example `vmware-svga`, `VGA`), the driver does not attach. Change the host QEMU configuration first.
- The package is not signed. macOS may show an "unidentified developer" warning during installation.
- Multiple monitors (`max_outputs` 2 or more) are not supported. Even if the device reports several outputs, only the first output is used.

## 2. Create a disk snapshot on the host

The driver runs in the kernel. If something goes wrong the system may not boot, so create a snapshot first **with the VM shut down**. Running `qemu-img snapshot` on a qcow2 while the VM is running can corrupt the image.

```bash
qemu-img snapshot -c before-virtiovga <guest disk image>.qcow2
```

If OpenCore is in a separate image (for example `OpenCore.qcow2`), create a snapshot of that image as well. If the disk is not qcow2, make a copy of the image file.

## 3. Download the package

Download `VirtioVGA-0.9.0.pkg` and `VirtioVGA-0.9.0.pkg.sha256` from Releases of the GitHub repository [good-ghost/OSX-VirtioVGA](https://github.com/good-ghost/OSX-VirtioVGA). Verify the downloaded file.

```bash
shasum -a 256 -c VirtioVGA-0.9.0.pkg.sha256
```

It must print `OK`. Both files must be in the same folder.

## 4. Install the package

Installing from the terminal is recommended (it does not go through the signing warning).

```bash
sudo installer -pkg VirtioVGA-0.9.0.pkg -target /
```

To install from Finder, right-click the file and choose **Open**. The package installs the following.

| Location | Contents |
| ---- | ---- |
| `/Library/Extensions/VirtioVGA.kext` | Main driver. After installation it is registered in the auxiliary kernel collection with `kmutil load -z -p` |
| `/Library/Application Support/VirtioVGA/` | Helper kext (`VirtioVGANdrvBlocker.kext`), `opencore-setup.sh`, `uninstall.sh`, `vvstat`, `vvresize` |
| `/Library/LaunchAgents/local.virtiovga.vvresize.plist` | Automatic window-size switching agent (starts at login) |

Even if the message "The install requires restarting now" appears, **do not reboot yet.** The OpenCore setup in the next step must be done first.

## 5. OpenCore setup (first time only)

```bash
sudo "/Library/Application Support/VirtioVGA/opencore-setup.sh"
```

The script does the following.

1. Finds and mounts the EFI partition that contains `EFI/OC/config.plist`, shows the path it found, and asks whether to continue. If it cannot find it, specify the device, for example `--efi disk0s1`.
2. Backs up `config.plist` to `config.plist.bak-<time>-virtiovga`.
3. Copies the helper kext `VirtioVGANdrvBlocker.kext` to `EFI/OC/Kexts` and adds an entry to `Kernel > Add`. This kext keeps the existing display driver (IONDRV) from attaching to the virtio GPU early in boot.
4. Sets `csr-active-config` to `0x201` (`01020000`, only Kext Signing disabled). This is needed to load the unsigned main driver from the auxiliary kernel collection.

To see only what would change, run it with `--dry-run`. When it finishes, the last line of the output shows the command to revert. Write down the backup file name.

## 6. Reboot and verify

```bash
sudo shutdown -r now
```

After booting, check the status.

```bash
"/Library/Application Support/VirtioVGA/vvstat"
```

- If the exit code is 0 and `init driver-ok` and `framebuffer: attached` are shown, everything is working.
- In `system_profiler SPDisplaysDataType` the GPU name shows as **Virtio GPU** and VRAM as **128 MB**.
- You can choose a resolution in System Settings > Displays (1024×768 to 2560×1600, including HiDPI).
- `csrutil status` must be Custom Configuration (Kext Signing: disabled).

## 7. Usage and settings

- **Automatic window-size switching**: when you resize the QEMU window on the host, the guest resolution follows. To turn it off, add the boot-arg `vvideo_autoresize=0` ([INSTALL.en.md §5.1](INSTALL.en.md)).
- **Settings**: the list of boot-args and the kext defaults are in [INSTALL.en.md §5](INSTALL.en.md).
- **Known limitations**: there is no Metal (GPU acceleration) and no transparency effects. Other limitations are in [INSTALL.en.md §8](INSTALL.en.md).

## 8. When the system does not boot or the screen looks wrong

Try the lightest method first.

1. **Disable only the driver**: add `-vvideooff` to boot-args from the OpenCore boot picker or the Recovery terminal; the driver then does not attach to the device and the system boots with the existing screen. How to do this is in [INSTALL.en.md §7](INSTALL.en.md).
2. **Revert config.plist**: in the Recovery terminal, copy the backup file from step 5 to `config.plist`.

   ```bash
   diskutil mount disk0s1
   ```

   ```bash
   cp /Volumes/EFI/EFI/OC/config.plist.bak-<time>-virtiovga /Volumes/EFI/EFI/OC/config.plist
   ```

3. **Restore the snapshot**: shut down the VM on the host and revert to the snapshot from step 2.

   ```bash
   qemu-img snapshot -a before-virtiovga <guest disk image>.qcow2
   ```

When reporting a problem, please send the `vvstat --log 40` output together with the panic reports in `/Library/Logs/DiagnosticReports` ([DEBUGGING.en.md](DEBUGGING.en.md)).

## 9. Removal

```bash
sudo "/Library/Application Support/VirtioVGA/uninstall.sh"
```

In order, it disables the helper kext in OpenCore (`Enabled false` after a backup), deletes the agent and the main driver, and then rebuilds the auxiliary kernel collection. Reboot when it finishes. If only the helper kext remains and the main driver is gone, the existing display driver is also blocked and no screen appears, so do not delete things by hand; use this script. The auxiliary kernel collection rebuild step of the removal script has not yet actually been run.

## 10. Upgrading from versions before 0.9.0

The names changed in 0.9.0 (`VirtioVideo.kext` → `VirtioVGA.kext`, `VirtioVideoNdrvBlocker.kext` → `VirtioVGANdrvBlocker.kext`, bundle id `local.virtiovideo.*` → `local.virtiovga.*`). Install in the same order as for a new VM (steps 4 to 6).

- The package deletes the old `/Library/Extensions/VirtioVideo.kext` and the old agent (`local.virtiovideo.vvresize`).
- `opencore-setup.sh` deletes the old `VirtioVideo*` entries in `Kernel > Add` and the old kext folders on the EFI, and adds the new entries.
- If you installed a per-user agent with `scripts/vvresize-agent.sh install` during development, the package deletes that too and replaces it with the global agent.

This procedure was verified on 2026-11-25 on a VM that was running 0.0.51.
