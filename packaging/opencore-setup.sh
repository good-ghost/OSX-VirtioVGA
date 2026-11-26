#!/bin/zsh
# VirtioVGA OpenCore 설정 (pkg가 /Library/Application Support/VirtioVGA에 설치합니다).
#
# 하는 일 (config.plist를 고치기 전에 백업합니다):
#   1. OpenCore가 있는 EFI 파티션을 찾아 마운트합니다 (EFI/OC/config.plist가 있는 첫 EFI, 또는 --efi로 지정).
#   2. 보조 kext VirtioVGANdrvBlocker.kext를 EFI/OC/Kexts에 복사합니다. 예전 이름(VirtioVideoNdrvBlocker.kext)은 지웁니다.
#   3. config.plist의 Kernel > Add에 VirtioVGANdrvBlocker 항목을 넣고, 예전 VirtioVideo* 항목은 지웁니다.
#   4. csr-active-config를 0x201(01020000, Kext Signing만 해제)로 두고, NVRAM > Delete에 넣어 부팅 때 적용되게 합니다.
#
# 사용법: sudo "/Library/Application Support/VirtioVGA/opencore-setup.sh" [--efi diskXsY] [--yes] [--dry-run] [--remove]
#   --remove: 제거용. Kernel > Add의 VirtioVGANdrvBlocker 항목을 끕니다(Enabled false). 본 kext를 지울 때 함께 해야
#             합니다. 보조 kext만 남으면 IONDRV도 막혀 화면이 나오지 않습니다. csr-active-config는 그대로 둡니다.
# 끝나면 재부팅해야 적용됩니다. 되돌리기: 출력에 나온 백업 파일을 config.plist로 복사합니다.
set -eu

readonly SUPPORT_DIR=${0:A:h}
readonly HELPER_NAME=VirtioVGANdrvBlocker
readonly OLD_HELPER_NAME=VirtioVideoNdrvBlocker
readonly HELPER_SRC="$SUPPORT_DIR/${HELPER_NAME}.kext"
readonly NVRAM_GUID=7C436110-AB2A-4BBB-A880-FE41995C9F82
readonly CSR_VALUE_BASE64=AQIAAA==   # 01020000 = 0x201 (little endian)
readonly MIN_KERNEL=24.0.0           # macOS 15

efi_device=""
assume_yes=0
dry_run=0
remove_mode=0
while (( $# > 0 )); do
    case $1 in
        --efi) efi_device=${2:?--efi needs a device, e.g. disk0s1}; shift 2 ;;
        --yes) assume_yes=1; shift ;;
        --dry-run) dry_run=1; shift ;;
        --remove) remove_mode=1; shift ;;
        *) print -u2 "usage: opencore-setup.sh [--efi diskXsY] [--yes] [--dry-run] [--remove]"; exit 2 ;;
    esac
done

if (( EUID != 0 )); then
    print -u2 "root 권한이 필요합니다: sudo $0 $*"
    exit 1
fi
if (( ! remove_mode )) && [[ ! -d $HELPER_SRC ]]; then
    print -u2 "보조 kext가 없습니다: $HELPER_SRC"
    exit 1
fi

mount_point_of() {  # mount_point_of <device> → 마운트 위치(없으면 빈 문자열)
    diskutil info "$1" 2>/dev/null | awk -F': *' '/Mount Point/ {print $2}'
}

# EFI 파티션 후보: 지정한 장치, 아니면 GPT의 EFI 파티션 전부.
candidates=()
if [[ -n $efi_device ]]; then
    candidates=("$efi_device")
else
    candidates=(${(f)"$(diskutil list | awk '$2 == "EFI" {print $NF}')"})
fi

config=""
for device in $candidates; do
    mount_point=$(mount_point_of "$device")
    if [[ -z $mount_point ]]; then
        diskutil mount "$device" >/dev/null 2>&1 || continue
        mount_point=$(mount_point_of "$device")
    fi
    if [[ -n $mount_point && -f "$mount_point/EFI/OC/config.plist" ]]; then
        config="$mount_point/EFI/OC/config.plist"
        efi_device=$device
        break
    fi
done
if [[ -z $config ]]; then
    print -u2 "OpenCore config.plist가 있는 EFI 파티션을 찾지 못했습니다. --efi diskXsY로 지정해 주세요."
    exit 1
fi
readonly OC_DIR=${config:h}
print "OpenCore: $efi_device → $config"

if (( ! assume_yes )); then
    print -n "이 config.plist를 고칩니다. 계속할까요? [y/N] "
    read -r answer
    [[ $answer == [yY]* ]] || { print "취소했습니다."; exit 0; }
fi

run() {  # dry-run이면 명령만 보여 줍니다
    if (( dry_run )); then
        print "(dry-run) $*"
    else
        "$@"
    fi
}

backup="$config.bak-$(date +%Y%m%d-%H%M%S)-virtiovga"
run cp -p "$config" "$backup"
print "백업: $backup"

if (( remove_mode )); then
    count=$(/usr/libexec/PlistBuddy -c "Print :Kernel:Add" "$config" 2>/dev/null | grep -c "BundlePath" || true)
    for (( i = 0; i < count; i++ )); do
        bundle=$(/usr/libexec/PlistBuddy -c "Print :Kernel:Add:${i}:BundlePath" "$config" 2>/dev/null || true)
        if [[ ${bundle} == ${HELPER_NAME}.kext || ${bundle} == ${OLD_HELPER_NAME}.kext ]]; then
            print "Kernel > Add[${i}] ${bundle} 항목을 끕니다"
            run /usr/libexec/PlistBuddy -c "Set :Kernel:Add:${i}:Enabled false" "$config"
        fi
    done
    (( dry_run )) || plutil -lint "$config"
    print "완료했습니다. 재부팅하면 IONDRV(기본 화면)로 부팅됩니다. 되돌리려면: sudo cp \"$backup\" \"$config\""
    exit 0
fi

# 2. 보조 kext 복사. zsh에서 "$var:x"는 수식어로 해석되므로(:c 등) 변수는 모두 ${var}로 씁니다.
# 예전 이름(0.9.0 이전)의 보조 kext와, 예전에 OpenCore로 주입하던 본 kext 폴더도 지웁니다.
run rm -rf "$OC_DIR/Kexts/${OLD_HELPER_NAME}.kext" "$OC_DIR/Kexts/VirtioVideo.kext" "$OC_DIR/Kexts/${HELPER_NAME}.kext"
run ditto --norsrc --noextattr "$HELPER_SRC" "$OC_DIR/Kexts/${HELPER_NAME}.kext"
run dot_clean -m "$OC_DIR" 2>/dev/null || true

# 3. Kernel > Add: 예전 VirtioVideo* 항목을 뒤에서부터 지우고, 새 항목이 없으면 끝에 넣습니다(Lilu·WhateverGreen 뒤).
count=$(/usr/libexec/PlistBuddy -c "Print :Kernel:Add" "$config" 2>/dev/null | grep -c "BundlePath" || true)
has_new=0
for (( i = count - 1; i >= 0; i-- )); do
    bundle=$(/usr/libexec/PlistBuddy -c "Print :Kernel:Add:${i}:BundlePath" "$config" 2>/dev/null || true)
    case ${bundle} in
        VirtioVideo.kext|${OLD_HELPER_NAME}.kext)
            print "Kernel > Add[${i}] ${bundle} 항목을 지웁니다"
            run /usr/libexec/PlistBuddy -c "Delete :Kernel:Add:${i}" "$config"
            ;;
        ${HELPER_NAME}.kext)
            has_new=1
            run /usr/libexec/PlistBuddy -c "Set :Kernel:Add:${i}:Enabled true" "$config"
            ;;
    esac
done
if (( ! has_new )); then
    entry='{"Arch":"x86_64","BundlePath":"'${HELPER_NAME}'.kext","Comment":"VirtioVGA: keep IONDRV off the virtio GPU","Enabled":true,"ExecutablePath":"Contents/MacOS/'${HELPER_NAME}'","MaxKernel":"","MinKernel":"'${MIN_KERNEL}'","PlistPath":"Contents/Info.plist"}'
    print "Kernel > Add에 ${HELPER_NAME} 항목을 넣습니다"
    run plutil -insert Kernel.Add -json "$entry" -append "$config"
fi

# 4. csr-active-config 0x201. PlistBuddy로 data를 넣으면 ASCII 문자열이 되므로 plutil을 씁니다.
if /usr/libexec/PlistBuddy -c "Print :NVRAM:Add:${NVRAM_GUID}:csr-active-config" "$config" >/dev/null 2>&1; then
    run plutil -replace "NVRAM.Add.${NVRAM_GUID}.csr-active-config" -data "$CSR_VALUE_BASE64" "$config"
else
    run plutil -insert "NVRAM.Add.${NVRAM_GUID}.csr-active-config" -data "$CSR_VALUE_BASE64" "$config"
fi
if ! /usr/libexec/PlistBuddy -c "Print :NVRAM:Delete:${NVRAM_GUID}" "$config" 2>/dev/null | grep -q "csr-active-config"; then
    run plutil -insert "NVRAM.Delete.${NVRAM_GUID}" -string csr-active-config -append "$config"
fi

secure_boot=$(/usr/libexec/PlistBuddy -c "Print :Misc:Security:SecureBootModel" "$config" 2>/dev/null || print "?")
if [[ $secure_boot != Disabled ]]; then
    print -u2 "주의: Misc > Security > SecureBootModel이 '$secure_boot'입니다. 서명 없는 kext가 막힐 수 있습니다. 문제가 생기면 Disabled로 바꿔 주세요."
fi

if (( ! dry_run )); then
    plutil -lint "$config"
fi
print "완료했습니다. 재부팅하면 적용됩니다. 되돌리려면: sudo cp \"$backup\" \"$config\""
