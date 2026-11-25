#!/bin/zsh
# 주기 전송 부하 측정 (WI-4.004). 변경 감지 전송을 끈 상태와 켠 상태에서 같은 시간 동안
# kernel_task CPU 평균과 VVDirtyStats 변화(보낸 MiB, 건너뛴 주기)를 남긴다.
#
# 사용법: scripts/measure-flush.sh <장면 이름> [초, 기본 30]
#   장면 이름 예: static, typing, video. 측정하는 동안 그 장면을 유지한다(정지 화면이면 손대지 않기).
# 결과: docs/research/p4-performance.log 에 한 줄씩 덧붙인다. 호스트 QEMU 프로세스 CPU는 호스트에서 따로 잰다.
set -u

readonly SCENE=${1:?usage: measure-flush.sh <scene> [seconds]}
readonly SECONDS_PER_MODE=${2:-30}
readonly ROOT=${0:A:h:h}
readonly LOG="$ROOT/docs/research/p4-performance.log"
readonly SAMPLE_INTERVAL_S=2

stats() {
    ioreg -rw0 -c VirtioGpuDevice -l | grep -m1 '"VVDirtyStats" =' | sed 's/.*= "//; s/"$//'
}

field() {  # field <stats 문자열> <이름>
    print -r -- "$1" | grep -o "$2=[0-9]*" | cut -d= -f2
}

kernel_cpu_avg() {
    local samples=$(( SECONDS_PER_MODE / SAMPLE_INTERVAL_S ))
    # 첫 표본은 누적값이라 버린다.
    top -l $(( samples + 1 )) -s $SAMPLE_INTERVAL_S -stats pid,cpu -pid 0 2>/dev/null |
        awk '$1 == "0" { n++; if (n > 1) { sum += $2; count++ } } END { if (count) printf "%.1f", sum / count; else print "na" }'
}

measure() {  # measure <0|1>
    "$ROOT/scripts/vvctl.sh" dirty "$1" >/dev/null || { print "vvctl failed" >&2; exit 1; }
    sleep 3  # 첫 주기의 전체 전송이 끝나게 둔다
    local before=$(stats)
    local cpu=$(kernel_cpu_avg)
    local after=$(stats)
    local sent=$(( $(field "$after" sent-mib) - $(field "$before" sent-mib) ))
    local full=$(( $(field "$after" full-mib) - $(field "$before" full-mib) ))
    local skipped=$(( $(field "$after" skipped) - $(field "$before" skipped) ))
    local partial=$(( $(field "$after" partial) - $(field "$before" partial) ))
    print "$(date '+%Y-%m-%d %H:%M:%S') scene=$SCENE dirty=$1 seconds=$SECONDS_PER_MODE kernel_task_cpu=$cpu sent_mib=$sent full_equiv_mib=$full partial=$partial skipped=$skipped scan_us_avg=$(field "$after" scan-us-avg)" | tee -a "$LOG"
}

mkdir -p "${LOG:h}"
sudo -v || exit 1  # vvctl이 root로 돈다. 암호는 처음 한 번만 묻는다
print "# $(date '+%Y-%m-%d %H:%M:%S') scene=$SCENE mode=$(system_profiler SPDisplaysDataType 2>/dev/null | awk -F': ' '/Resolution/{print $2; exit}')"
print "[1/2] 변경 감지 끔, ${SECONDS_PER_MODE}초 측정"
measure 0
print "[2/2] 변경 감지 켬, ${SECONDS_PER_MODE}초 측정"
measure 1
"$ROOT/scripts/vvctl.sh" dirty 0 >/dev/null
print "측정 끝. 변경 감지는 다시 껐다."
