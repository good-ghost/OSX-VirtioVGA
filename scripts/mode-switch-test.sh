#!/bin/zsh
# 모드 전환 반복·누수 시험 (WI-5.002, NFR-002). 사용법: scripts/mode-switch-test.sh [횟수, 기본 100] [간격 ms, 기본 1500]
# 전후로 드라이버 객체 수(ioclasscount), 고정 메모리, 드라이버 카운터를 남기고 그 사이에 mode-cycle을 돌린다.
# 결과: docs/research/p5-mode-switch.log
set -u
readonly COUNT=${1:-100}
readonly INTERVAL_MS=${2:-1500}
readonly ROOT=${0:A:h:h}
readonly LOG="$ROOT/docs/research/p5-mode-switch.log"
readonly BIN="$ROOT/build/mode-cycle"
readonly CLASSES=(VirtioGpuDevice VirtioVideoFramebuffer VirtioQueue VVDmaBuffer VirtioPciTransport IOBufferMemoryDescriptor)

if [[ ! -x "$BIN" || "$ROOT/scripts/mode-cycle.swift" -nt "$BIN" ]]; then
    mkdir -p "${BIN:h}" && swiftc -O -o "$BIN" "$ROOT/scripts/mode-cycle.swift" || exit 1
fi

snapshot() {  # snapshot <label>
    print "## $1 $(date '+%H:%M:%S')"
    print "classes: $(ioclasscount ${CLASSES[@]})"
    print "wired-pages: $(vm_stat | awk '/Pages wired down/ {gsub(/\./,"",$4); print $4}')"
    ioreg -rw0 -c VirtioGpuDevice -l | grep -E '"(VVScanout|VVCurrentMode|VVEventCounters|VVRecoveries)"' | sed 's/^[ |]*/  /'
    print "panics: $(ls /Library/Logs/DiagnosticReports/*.panic 2>/dev/null | wc -l | tr -d ' ')"
}

mkdir -p "${LOG:h}"
{
    print "# $(date '+%Y-%m-%d %H:%M:%S') mode switch test count=$COUNT interval=${INTERVAL_MS}ms kext=$(kmutil showloaded 2>/dev/null | awk '/local.virtiovga.VirtioVGA /{print $7}')"
    snapshot before
    "$BIN" "$COUNT" "$INTERVAL_MS"
    print "mode-cycle exit=$?"
    sleep 5
    snapshot after
} 2>&1 | tee -a "$LOG"
