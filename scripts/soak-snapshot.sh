#!/bin/zsh
# 장시간 사용 시험 기록 (WI-5.001, NFR-001). 사용법: scripts/soak-snapshot.sh <start|end|메모>
# 시각·가동 시간·패닉 보고서·드라이버 카운터·객체 수·고정 메모리를 docs/research/p5-soak.log에 덧붙인다.
set -u
readonly LABEL=${1:?usage: soak-snapshot.sh <label>}
readonly ROOT=${0:A:h:h}
readonly LOG="$ROOT/docs/research/p5-soak.log"
mkdir -p "${LOG:h}"
{
    print "## $LABEL $(date '+%Y-%m-%d %H:%M:%S')"
    print "uptime: $(uptime | sed 's/^ *//')"
    print "kext: $(kmutil showloaded 2>/dev/null | awk '/local.virtiovideo.VirtioVideo /{print $7}')"
    print "panic-reports: $(ls -1 /Library/Logs/DiagnosticReports/*.panic 2>/dev/null | wc -l | tr -d ' ') latest=$(ls -t /Library/Logs/DiagnosticReports/*.panic 2>/dev/null | head -1 | xargs basename 2>/dev/null)"
    print "classes: $(ioclasscount VirtioGpuDevice VirtioVideoFramebuffer VirtioQueue VVDmaBuffer)"
    print "wired-pages: $(vm_stat | awk '/Pages wired down/ {gsub(/\./,"",$4); print $4}')"
    ioreg -rw0 -c VirtioGpuDevice -l | grep -E '"(VVInitResult|VVScanout|VVEventCounters|VVDisplayPower|VVCursor|VVRecoveries|VVCurrentMode)"' | sed 's/^[ |]*/  /'
} | tee -a "$LOG"
