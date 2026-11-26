#!/usr/bin/env bash

# Special thanks to:
# https://github.com/Leoyzen/KVM-Opencore
# https://github.com/thenickdude/KVM-Opencore/
# https://github.com/qemu/qemu/blob/master/docs/usb2.txt
#
# qemu-img create -f qcow2 mac_hdd_ng.img 128G
#
# echo 1 > /sys/module/kvm/parameters/ignore_msrs (this is required)

###############################################################################
# NOTE: Tweak the "MY_OPTIONS" line in case you are having booting problems!
###############################################################################
#
# Change `Penryn` to `Haswell-noTSX` in OpenCore-Boot.sh file for macOS Sonoma!
#
###############################################################################


MY_OPTIONS="+ssse3,+sse4.2,+popcnt,+avx,+aes,+xsave,+xsaveopt,check"

# This script works for Big Sur, Catalina, Mojave, and High Sierra. Tested with
# macOS 10.15.6, macOS 10.14.6, and macOS 10.13.6.

ALLOCATED_RAM="32768" # MiB
CPU_SOCKETS="1"
CPU_CORES="2"
CPU_THREADS="8"

REPO_PATH="."
OVMF_DIR="."

# shellcheck disable=SC2054
args=(
  # -enable-kvm -m "$ALLOCATED_RAM" -cpu Penryn,kvm=on,vendor=GenuineIntel,+invtsc,vmware-cpuid-freq=on,"$MY_OPTIONS"
  -enable-kvm -m "$ALLOCATED_RAM" -cpu Skylake-Client-v4,-hle,-pcid,-rtm,kvm=on,vendor=GenuineIntel,+invtsc,vmware-cpuid-freq=on,"$MY_OPTIONS"  # ATTENTION: Enable this line for macOS Sequoia and Tahoe
  -machine pc-q35-10.0
  -device qemu-xhci,id=xhci
  -device usb-kbd,bus=xhci.0 -device usb-tablet,bus=xhci.0
  -smp "$CPU_THREADS",cores="$CPU_CORES",sockets="$CPU_SOCKETS"
  -device usb-ehci,id=ehci
  -device isa-applesmc,osk="ourhardworkbythesewordsguardedpleasedontsteal(c)AppleComputerInc"
  -drive if=pflash,format=raw,readonly=on,file="$REPO_PATH/$OVMF_DIR/OVMF_CODE_4M.fd"
  -drive if=pflash,format=raw,file="$REPO_PATH/$OVMF_DIR/OVMF_VARS-1920x1080.fd"
  -smbios type=2
  -device ich9-intel-hda -device hda-duplex
  -device ich9-ahci,id=sata
  #-drive id=OpenCoreBoot,if=none,snapshot=on,format=qcow2,file="$REPO_PATH/OpenCore/OpenCore.qcow2"
  #-device ide-hd,bus=sata.1,drive=OpenCoreBoot
  #-drive id=InstallMedia,if=none,file="$REPO_PATH/BaseSystem.img",format=raw
  #-device ide-hd,bus=sata.2,drive=InstallMedia
  # All libvirt snapshots (WI-2.006.completed, phase3.completed) were merged into macOS.img (2026-11-26). macOS.img is booted directly, without an overlay.
  -drive id=MacHDD,if=none,file="$REPO_PATH/macOS.img",format=qcow2,cache=writeback,aio=threads
  -device ide-hd,bus=sata.3,drive=MacHDD,bootindex=1
  -netdev user,id=net0,hostfwd=tcp::2222-:22 -device virtio-net-pci,netdev=net0,id=net0,mac=52:54:00:c9:18:27
  -monitor stdio
  -device virtio-vga-gl
  -display gtk,gl=on
  -gdb tcp:127.0.0.1:1234
)

qemu-system-x86_64 "${args[@]}"
