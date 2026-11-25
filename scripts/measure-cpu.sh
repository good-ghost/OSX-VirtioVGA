#!/bin/zsh
# 화면 드라이버 CPU 부하 측정 (WI-5.004, NFR-004). 사용법: scripts/measure-cpu.sh <이름> [초, 기본 60]
# 측정하는 동안 화면을 건드리지 않는다. 2초 간격 top 표본의 평균을 docs/research/p5-performance.log에 남긴다.
#   kernel_task: 드라이버의 주기 전송 루프가 이 안에서 돈다 (드라이버 비용을 가장 직접 보여 준다)
#   WindowServer, 시스템 전체(user+sys): 참고값. Claude 앱 등 다른 프로세스의 영향을 받는다
set -u
readonly LABEL=${1:?usage: measure-cpu.sh <label> [seconds]}
readonly SECONDS_TOTAL=${2:-60}
readonly INTERVAL_S=2
readonly ROOT=${0:A:h:h}
readonly LOG="$ROOT/docs/research/p5-performance.log"

samples=$(( SECONDS_TOTAL / INTERVAL_S + 1 ))
resolution=$(system_profiler SPDisplaysDataType 2>/dev/null | awk -F': ' '/Resolution/{r=$2} /Looks like/{l=$2} END{print r " (looks like " l ")"}')
if "$ROOT/scripts/vvstat.sh" >/dev/null 2>&1; then driver="virtiovideo"; else driver="iondrv-or-off"; fi
# 첫 표본은 누적값이라 버린다.
result=$(top -l $samples -s $INTERVAL_S -o cpu -n 20 -stats pid,command,cpu 2>/dev/null | awk '
    /^CPU usage/ { n++; if (n > 1) { gsub(/%/, ""); user += $3; sys += $5; cpu++ } next }
    n > 1 && $1 == "0" { kt += $NF; ktn++ }
    n > 1 && $2 == "WindowServer" { ws += $NF; wsn++ }
    END { printf "system_user+sys=%.1f kernel_task=%.1f WindowServer=%.1f samples=%d", (user+sys)/cpu, kt/ktn, ws/wsn, cpu }')
mkdir -p "${LOG:h}"
print "$(date '+%Y-%m-%d %H:%M:%S') label=$LABEL driver=$driver resolution=\"$resolution\" seconds=$SECONDS_TOTAL $result" | tee -a "$LOG"
