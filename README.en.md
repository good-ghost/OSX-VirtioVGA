# VirtioVGA

English | [한국어](README.md)

A **virtio-gpu 2D framebuffer driver (kext)** for macOS guests running on OSX-KVM (QEMU/KVM).

Without a driver, the guest draws the screen with the boot framebuffer (`IONDRVFramebuffer`) handed over by the firmware (OVMF GOP). In that case the resolution is fixed to a single mode and VRAM is shown as "3 MB". VirtioVGA provides an `IOFramebuffer` that builds the screen with virtio-gpu commands.

## Features

- 10 resolutions (1024×768 to 2560×1600), the device's preferred resolution, HiDPI, mode restore after reboot
- Automatic resolution switching to match the host QEMU window size (agent `vvresize`)
- Reports 128 MB VRAM, GPU name "Virtio GPU", monitor identification through EDID ("QEMU Monitor")
- Hardware cursor (virtio cursorq), switch to a software cursor with a boot-arg
- Display sleep: stops transfers and turns the screen off, restores on wake
- Automatic recovery on device errors; falls back to the existing screen (IONDRV) if initialization fails
- Read-only status tool `vvstat`

Metal (GPU acceleration) is out of scope. Transparency effects and wallpapers drawn with Metal (Sequoia, etc.) do not work. Multiple monitors are not supported (WI-6.002 on hold).

## Target environment

- macOS 15.x x86_64 (verified: 15.8.3 `24H241`), OpenCore 1.0.6. Verified to work together with Lilu 1.6.8 and WhateverGreen 1.6.7.
- QEMU 10.0.13 `-device virtio-vga` (or `virtio-vga-gl`), GTK display
- SIP `csr-active-config` 0x201 (only Kext Signing disabled)

## Installation

- **New VM**: install with `VirtioVGA-0.9.0.pkg` from GitHub Releases. Follow the steps in [docs/NEW-VM-INSTALL.en.md](docs/NEW-VM-INSTALL.en.md). Install the package, run `opencore-setup.sh` once, then reboot.
- **Build from source and install/replace** (for development), settings, rollback: [docs/INSTALL.en.md](docs/INSTALL.en.md)
- **When something goes wrong**: [docs/DEBUGGING.en.md](docs/DEBUGGING.en.md)

## Build

```bash
scripts/build.sh
```

```bash
scripts/test.sh
```

```bash
packaging/build-pkg.sh
```

This produces `build/VirtioVGA.kext`, `build/VirtioVGANdrvBlocker.kext`, and `build/pkg/VirtioVGA-<version>.pkg`. Xcode (command line tools, macOS SDK) is required. The package is not signed.

## Documents

| Document | Contents |
| ---- | ---- |
| [docs/NEW-VM-INSTALL.en.md](docs/NEW-VM-INSTALL.en.md) | Installing on a new VM with the pkg |
| [docs/INSTALL.en.md](docs/INSTALL.en.md) | Development install/replace, settings, rollback, known limitations |
| [docs/DEBUGGING.en.md](docs/DEBUGGING.en.md) | Status queries, logs, panic analysis |
| [docs/REQUIREMENT.en.md](docs/REQUIREMENT.en.md) | Requirements (v1.4) |
| [docs/PROJECT-PLAN.en.md](docs/PROJECT-PLAN.en.md) | Project plan and progress |
| [docs/ACCEPTANCE.en.md](docs/ACCEPTANCE.en.md) | Final acceptance criteria check results |

The documents directly under `docs/` have English versions (`*.en.md`) in the same folder.

## Directories

| Path | Purpose |
| ---- | ---- |
| `kext/` | Main driver source. `kext/include/vv/` holds logic that does not depend on the kernel (unit test target) |
| `kext-helper/` | IONDRV blocking helper kext (OpenCore injection) |
| `tools/` | User-space tools (`vvstat.c`, `vvresize.swift`) |
| `packaging/` | Installer package build, OpenCore setup and removal scripts |
| `scripts/` | Build, testing, measurement, environment collection, panic summary |
| `tests/` | Unit tests (`scripts/test.sh`) |
| `docs/` | Requirements, plans, installation and debugging documents |

## Status (2026-11-25, 0.9.0)

- P0 to P5 complete, acceptance criteria AC-01 to 09 passed (M3). P6 automatic window-size switching (WI-6.001) complete, multiple monitors (WI-6.002) on hold
- In 0.9.0 the name changed from Virtio-Video (`VirtioVideo.kext`) to VirtioVGA (`VirtioVGA.kext`), and an installer package was added.

## License

MIT — [LICENSE](LICENSE)
