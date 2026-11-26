#!/bin/zsh
# 창 크기 자동 전환 에이전트(tools/vvresize.swift)를 로그인한 사용자의 LaunchAgent로 설치·제거한다 (WI-6.001).
# 사용법: scripts/vvresize-agent.sh install | uninstall | status
# 사용자 영역에만 쓰므로 sudo가 필요 없다. 로그: ~/Library/Logs/vvresize.log
set -eu
readonly ROOT=${0:A:h:h}
readonly LABEL=local.virtiovga.vvresize
readonly SRC="$ROOT/tools/vvresize.swift"
readonly INSTALL_DIR="$HOME/Library/Application Support/VirtioVGA"
readonly BIN="$INSTALL_DIR/vvresize"
readonly PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
readonly LOG="$HOME/Library/Logs/vvresize.log"
readonly DOMAIN="gui/$(id -u)"

unload() {
    launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
}

install_agent() {
    mkdir -p "$INSTALL_DIR" "${PLIST:h}" "${LOG:h}"
    swiftc -O -o "$BIN" "$SRC"
    cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>Label</key>
	<string>$LABEL</string>
	<key>ProgramArguments</key>
	<array>
		<string>$BIN</string>
	</array>
	<key>RunAtLoad</key>
	<true/>
	<key>KeepAlive</key>
	<true/>
	<key>ProcessType</key>
	<string>Background</string>
	<key>StandardOutPath</key>
	<string>$LOG</string>
	<key>StandardErrorPath</key>
	<string>$LOG</string>
</dict>
</plist>
EOF
    plutil -lint "$PLIST"
    unload
    launchctl bootstrap "$DOMAIN" "$PLIST"
    print "installed: $BIN, $PLIST"
}

case ${1:-} in
    install) install_agent ;;
    uninstall)
        unload
        rm -f "$PLIST" "$BIN"
        print "removed $LABEL (log kept: $LOG)"
        ;;
    status)
        launchctl print "$DOMAIN/$LABEL" 2>/dev/null | grep -E "state =|pid =|last exit" || print "$LABEL not loaded"
        [[ -f "$LOG" ]] && tail -5 "$LOG"
        ;;
    *)
        print -u2 "usage: vvresize-agent.sh install | uninstall | status"
        exit 2
        ;;
esac
