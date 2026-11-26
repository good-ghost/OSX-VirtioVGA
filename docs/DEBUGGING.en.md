# Debugging Procedures

English | [한국어](DEBUGGING.md)

Target: OSX-KVM guest macOS 15.8.3 (build `24H241`, x86_64), with `VirtioVGA.kext` loaded from the auxiliary kernel collection and `VirtioVGANdrvBlocker.kext` injected by OpenCore. As of 2026-11-25 (WI-5.009).
For installation, disabling, and rollback, see [INSTALL.md](INSTALL.en.md).

| Situation | Method | Status |
| ---- | ------- | ---- |
| Checking driver behavior | §1 `scripts/vvstat.sh`, driver log (`VVLog`) | Confirmed (WI-0.005, WI-5.008) |
| Kernel panic | §2 panic report + `scripts/panic-summary.py` | Confirmed (with a synthetic report) |
| When it hangs or the state must be inspected directly | §3 QEMU gdbstub + lldb (on the host) | kext symbols and memory reads confirmed. Kernel symbols not possible, breakpoints not confirmed |
| Checking Apple code behavior (IONDRV, IOFramebuffer) | §3.5 static lookup with KDK symbols (in the guest) | Confirmed |

## 1. Driver state and log

### 1.1 Status query tool

```bash
scripts/vvstat.sh --log 20
```

Reads IORegistry properties only (no root needed). Exit code 0 means `driver-ok` and the framebuffer is attached. `--all` shows all `VV*` properties.

| Line | Property | Normal example | What to look at |
| -- | ---- | ------- | ----- |
| `init` | `VVInitResult` | `driver-ok` | Any other value is the name of the initialization step that failed (e.g. `transport`, `interrupts`, `recovery-limit`) |
| `mode` | `VVCurrentMode` | `2560x1600 changes=1` | Resolution and number of switches |
| `scanout` | `VVScanout` | `running … flush-errors=0 blanks=0` | If `flush-errors` increases, transfers are failing (the resource is recreated). `blanks` is the number of display sleeps |
| `display power` | `VVDisplayPower` | `on sleeps=N wakes=N` | The sleep and wake counts should be equal. `-` if it has never slept |
| `cursor` | `VVCursor` | `… drops=0 rejects=0 scale=50` | `drops` (moves discarded because of interrupt context, etc.), `rejects` (images larger than 64×64) |
| `dirty flush` | `VVDirtyStats` | `on=0 …` | Change-detection transfer statistics (off by default) |
| `auto resize` | `VVAutoResizeState` | `on handler=enabled connect-changes=N last=WxH` | Automatic switching to the window size (INSTALL §5.4). `handler=none` means IOGraphics did not register the connection change callback |
| `edid` | `VVEdid` | `ok size=1024` | `invalid:…` means the EDID given by the device was rejected |
| `events` | `VVEventCounters` | `… device-errors=0 … timeouts=0 errors=1 recoveries=0` | `errors=1` is an error created on purpose by the self-test at boot. If `timeouts`, `device-errors`, or `recoveries` increase, it is a device problem |

### 1.2 Driver log

`IOLog`/`os_log` from a kext loaded through the auxiliary kernel collection or OpenCore does not appear in the unified log or `dmesg` (WI-0.005 measurement). The driver accumulates log lines in an internal ring buffer (128 lines) and publishes them as the IORegistry property `VVLog`. View it with `vvstat --log N` or read it directly.

```bash
ioreg -r -c VirtioGpuDevice -l -w0 | grep '"VV'
```

- Classes: `VirtioGpuDevice` (device, transport, cursor), `VirtioVideoFramebuffer` (`VVFbTrace` order of IOGraphics calls, `VVFbPowerTrace` last 24 power/connection attributes), `VirtioVideoProbe`. `VirtioGpuDevice` does not call `registerService`, so it cannot be found with `IOServiceGetMatchingService` (`vvstat` finds it by walking the registry).
- Level: boot-args `vvideo_log=0|1|2` (0 error, 1 info — default, 2 debug). [INSTALL.md §5.1](INSTALL.en.md)
- The log is lost on reboot. If the system hangs and is forced off, the state just before cannot be seen (WI-4.003, WI-5.003), so when reproducing, read `vvstat` in advance from another device through Remote Control, or record it with `scripts/soak-snapshot.sh`.
- To view the system log, use `/usr/bin/log show ...`. In zsh, `log` is a builtin command and fails silently.

## 2. Kernel panic

### 2.1 Report location

`/Library/Logs/DiagnosticReports/Kernel-<date>-<time>.panic` — readable as a normal user (no sudo needed).
Format (measured on macOS 15): line 1 is metadata JSON, body JSON from line 2. The panic string is `macOSPanicString` in the body.

### 2.2 Summary and source line conversion

```bash
python3 -I scripts/panic-summary.py
```

With no argument, it reads the most recent report. For a specific file, pass the path as an argument.

Output:

- Time, OS version, first 3 lines of the panic string
- If this kext (`local.virtiovga.VirtioVGA`) is in "Kernel Extensions in backtrace": version, UUID, load range
- Backtrace addresses within that range converted with `atos` to function, file, and line

### 2.3 dSYM and UUID

- `scripts/build.sh` creates `build/VirtioVGA.kext.dSYM` and also archives it in `build/dsym-archive/<UUID>.dSYM`.
- **The UUID changes with every build** (apparently because object file timestamps go into the debug map). So a dSYM with the same UUID as the panic report is needed, and if the current dSYM does not match, `panic-summary.py` looks it up by UUID in the archive.
- The dSYM of the installed build must be in the archive. Deleting `build/` also deletes the archive.
- To convert by hand:

  ```bash
  atos -o build/dsym-archive/<UUID>.dSYM/Contents/Resources/DWARF/VirtioVGA -arch x86_64 -l <kext start address> <address> ...
  ```

### 2.4 Panics during development (this project)

| Report | Cause | Handling |
| ------ | ---- | ---- |
| `Kernel-2026-11-24-052540.panic` | Runtime IONDRV `terminate()` → NULL dereference in `AppleUpstreamUserClientDriver::fbNotificationOccurred` | Dropped runtime takeover and switched to blocking IONDRV at boot with the helper kext. `vvideo_takeover=1` is kept for testing only |
| `Kernel-2026-11-24-054128.panic` | Same takeover test | Same |
| (no report) 2026-11-25 01:49 | `kmutil unload` while the screen is in use → froze in a panic state, forced power-off | NFR-003 changed to replace-by-reboot (WI-5.003) |

### 2.5 Panics that already existed on this VM (unrelated to this project)

There are `Kernel-*.panic` files from 2026-11-19, 11-22, and 11-23 18:53. All are from before the driver was installed, and the OS at the time was macOS 15.7.9 (24G830). The panic string is `mp_kdp_enter() timed-out` → `NMIPI for spinlock acquisition timeout`, and the backtrace is `xcpm_*` (CPU power management). The cause was not investigated. If the same panic occurs during driver testing, it may not be caused by this driver, so compare it with the pattern above.

## 3. QEMU gdbstub + lldb (run on the host)

The guest kernel cannot be debugged from inside the guest (the debugger itself stops). **Attach lldb on the host (Linux) to the QEMU gdbstub.** The procedure in this section has not been run yet.

### 3.1 KDK installation (guest, user task) — completed 2026-11-23

Installation result: `/Library/Developer/KDKs/KDK_15.8.3_24H241.kdk` (Apple Developer `Kernel_Debug_Kit_15.8.3_build_24H241.dmg`, 925,610,504 bytes, Apple-signed package). The UUIDs of the running kernel and the KDK kernel are the same, `747934E9-FD01-36AB-8E97-06A578AB22F9`. The KDK contains `kernel.dSYM` and `.kext.dSYM` for `IOGraphicsFamily`, `IONDRVSupport`, and `IOPCIFamily`.

Procedure (reinstall for the new build when the OS is updated):

1. Sign in with an Apple ID at https://developer.apple.com/download/all/ and search for "Kernel Debug Kit".
2. Download the one for **macOS 15.8.3, build 24H241**. If the same build is not available, download the closest 15.x build and record that fact (R-07). If the build differs, the kernel symbols may not match.
3. Install the `.pkg` (administrator password required). Install location: `/Library/Developer/KDKs/KDK_<version>_<build>.kdk`
4. Check:

   ```bash
   ls /Library/Developer/KDKs
   ```

   ```bash
   dwarfdump --uuid /System/Library/Kernels/kernel
   ```

   ```bash
   dwarfdump --uuid /Library/Developer/KDKs/<KDK name>.kdk/System/Library/Kernels/kernel
   ```

   The two UUIDs must match for the KDK kernel symbols to be usable.

### 3.2 QEMU options (host, user task) — applied 2026-11-23

Added `-gdb tcp:127.0.0.1:1234` to the QEMU arguments of the OSX-KVM launch script. `-s` (= `-gdb tcp::1234`) accepts connections on all interfaces without authentication, so it is not used. Use `-S` as well only when it must stop from the very start of boot (CPUs halt until gdb issues `continue`).

### 3.3 Copying symbols to the host

The host's lldb is in the distrobox container `gen-dev` (LLVM lldb 19.1.7, with `plugin.dynamic-loader.darwin-kernel`). The container shares the host network, so it reaches the gdbstub at `127.0.0.1:1234`.

1. Guest: turn on System Settings > General > Sharing > Remote Login (user task). Guest IP: `ipconfig getifaddr en0` (2026-11-23: `192.168.122.75`).
2. Guest: create the symbol bundle. The file name includes the kext UUID.

   ```bash
   U=$(dwarfdump --uuid build/VirtioVGA.kext/Contents/MacOS/VirtioVGA | awk '{print $2}'); K=/Library/Developer/KDKs/KDK_15.8.3_24H241.kdk/System/Library/Kernels; D=build/debug-symbols; rm -rf $D && mkdir -p $D/vv-debug && cp -R $K/kernel $K/kernel.dSYM build/VirtioVGA.kext/Contents/MacOS/VirtioVGA $D/vv-debug/ && cp -R build/dsym-archive/$U.dSYM $D/vv-debug/VirtioVGA.dSYM && xattr -cr $D/vv-debug && COPYFILE_DISABLE=1 tar -C $D -czf $D/vv-debug-$U.tar.gz vv-debug
   ```

3. Host (`distrobox enter gen-dev`): fetch it with `scp` and extract it.

   ```bash
   mkdir -p ~/vv-debug-host && cd ~/vv-debug-host && scp woomg@<guest IP>:Projects/VirtioVGA/build/debug-symbols/vv-debug-<UUID>.tar.gz . && tar -xzf vv-debug-<UUID>.tar.gz
   ```

### 3.4 lldb connection (host) — confirmed 2026-11-23

**Warning: do not end lldb with `quit`/Ctrl-D while attached.** If lldb sends a kill, QEMU may exit and the VM may shut down. Always `process detach` and then `quit`. For the same reason, do not attach with `--batch`. The whole VM is stopped while attached.

**Kernel symbols cannot be used.** lldb 19.1.7 (Linux) crashes with a segmentation fault the moment it opens the KDK kernel binary. The same happened with the dSYM moved away, with `--no-dependents`, and with `target.load-script-from-symbol-file false`. The same 4~5 frames repeat in the stack dump, so it looks like infinite recursion (a guess). So automatic kernel discovery is turned off and only the kext is attached. The kernel side of a backtrace appears as addresses only. Kernel addresses can be looked up with §3.5 (KDK static lookup) in the guest, but the KASLR slide must be known separately.

In the guest, check the kext load address (`Address` column).

```bash
kmutil showloaded | grep -i virtiovga
```

Host lldb (one line at a time):

```
settings set plugin.dynamic-loader.darwin-kernel.scan-type none
settings set plugin.dynamic-loader.darwin-kernel.load-kexts false
target create --no-dependents vv-debug/VirtioVGA
target symbols add vv-debug/VirtioVGA.dSYM/Contents/Resources/DWARF/VirtioVGA
gdb-remote 127.0.0.1:1234
target modules load --file VirtioVGA --slide <Address value from kmutil>
memory read -f s -c 1 <Address + kmod_info offset + 0x10>
process detach
quit
```

- The vmaddr of the kext `__TEXT` is 0, so the load address is the slide as is.
- `kmod_info` offset: `nm build/VirtioVGA.kext/Contents/MacOS/VirtioVGA | grep ' _kmod_info$'`. The struct's `name` field is at +0x10, so the bundle id string should be read.
- Giving `target symbols add` the `.dSYM` bundle path failed with "does not match any existing module". It is written in the form that gives the path of the DWARF file inside the bundle (this form has not been run yet).

2026-11-23 result (kext 0.0.5, UUID `935ADD8D-1FAD-37A7-A84E-72A47E4D92F7`, load address `0xffffff80040e4000`):

| Step | Result |
| ---- | ---- |
| `gdb-remote 127.0.0.1:1234` | Connected, `stop reason = signal SIGTRAP`. The first frame address was shown as `0xfff0`, but `register read rip` gave `0xffffff801d739d29` (a kernel address) |
| `target modules load --slide` | Succeeded |
| `memory read -f s 0xffffff80040e69a0` (`kmod_info`+0x10) | `"local.virtiovga.VirtioVGA"` — running memory and kext symbol addresses match |
| `process detach` | VM resumes |

Not confirmed

- **Stopping at a breakpoint**: currently the kext code only runs `start()` at boot and has no path that is called at runtime, so this was not tested. Once interrupt and timer paths exist after P1, confirm with `breakpoint set --name <function>`.
- **Stopping in `start()` during boot**: the address is not known before the kext loads. A way to start with `-S` and stop right after the kext loads has not been found yet. Investigate when it becomes necessary. Until then, use the §1 log and the §2 panic analysis.
- **Kernel symbols**: whether a newer lldb (LLVM 20 or later) can open the kernel binary has not been tested (installing a new package in the container is required).

### 3.5 Static lookup with KDK symbols (in the guest, no host needed)

Function locations and source lines of Apple code can be viewed without attaching to the running kernel. Used in P2 to investigate how `IONDRVFramebuffer` steps aside (WI-2.006).

```bash
lldb --batch -o "target create /Library/Developer/KDKs/KDK_15.8.3_24H241.kdk/System/Library/Kernels/kernel" -o "image lookup -n PE_parse_boot_argn"
```

```bash
lldb --batch -o "target create /Library/Developer/KDKs/KDK_15.8.3_24H241.kdk/System/Library/Extensions/IOGraphicsFamily.kext/IOGraphicsFamily" -o "image lookup -r -n 'IONDRVFramebuffer::'"
```

Check: the first command returns `PE_parse_boot_argn at bootargs.c:196`.
