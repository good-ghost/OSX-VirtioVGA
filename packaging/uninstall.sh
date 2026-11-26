#!/bin/zsh
# VirtioVGA 제거 (pkg가 /Library/Application Support/VirtioVGA에 설치합니다).
# 사용법: sudo "/Library/Application Support/VirtioVGA/uninstall.sh"
#
# 순서가 중요합니다. 먼저 OpenCore의 보조 kext를 끕니다. 보조 kext만 남고 본 kext가 없으면 IONDRV도 막혀 화면이
# 나오지 않습니다. 그다음 본 kext와 에이전트를 지우고 보조 kernel collection을 다시 만듭니다. 끝나면 재부팅합니다.
set -eu

readonly SUPPORT_DIR="/Library/Application Support/VirtioVGA"
readonly KEXT=/Library/Extensions/VirtioVGA.kext
readonly AGENT_LABEL=local.virtiovga.vvresize
readonly AGENT_PLIST=/Library/LaunchAgents/$AGENT_LABEL.plist

if (( EUID != 0 )); then
    print -u2 "root 권한이 필요합니다: sudo $0"
    exit 1
fi

"$SUPPORT_DIR/opencore-setup.sh" --remove "$@"

console_user=$(stat -f %Su /dev/console)
if [[ -n $console_user && $console_user != root ]]; then
    launchctl bootout "gui/$(id -u "$console_user")/$AGENT_LABEL" 2>/dev/null || true
fi
rm -f "$AGENT_PLIST"
rm -rf "$KEXT"
# 지운 kext를 보조 kernel collection에서 빼기 위해 다시 만듭니다.
kmutil create -n aux --no-boot --no-system-collection 2>/dev/null || kmutil rebuild 2>/dev/null || true
pkgutil --forget local.virtiovga.pkg >/dev/null 2>&1 || true
rm -rf "$SUPPORT_DIR"
print "제거했습니다. 재부팅해 주세요."
