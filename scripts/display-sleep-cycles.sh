#!/bin/zsh
# AC-09 (WI-4.002): 디스플레이 잠자기 → 깨우기 → 잠금 화면 → 로그인 반복 시험.
#
# 사용자가 직접 실행한다. 각 회차마다 스크립트가 `pmset displaysleepnow`로 재우고, 사용자가 30초쯤 뒤
# 키를 눌러 깨운 다음 로그인하면, 드라이버 카운터와 스크린샷을 남기고 다음 회차로 넘어간다.
# Claude 세션이 활동 중이면 디스플레이가 바로 깨어날 수 있으므로(0.0.31 실측) 사람이 키보드 앞에서 돌린다.
#
# 사용법: scripts/display-sleep-cycles.sh [회차 수, 기본 10]
set -u

readonly CYCLES=${1:-10}
readonly ROOT=${0:A:h:h}
readonly SHOT_DIR="$ROOT/docs/screenshots/p4"
readonly LOG="$ROOT/docs/research/p4-display-sleep-ac09.log"
readonly SLEEP_TIMEOUT_S=15   # 재운 뒤 드라이버가 꺼짐을 기록할 때까지 기다리는 최대 시간
readonly SETTLE_S=3           # 로그인 뒤 화면이 다 그려질 때까지 기다리는 시간

# VirtioGpuDevice 속성에서 숫자 하나를 꺼낸다. 예: counter VVDisplayPower sleeps
counter() {
    ioreg -rw0 -c VirtioGpuDevice -l | grep -m1 "\"$1\" =" | grep -o "$2=[0-9]*" | cut -d= -f2
}

screen_locked() {
    ioreg -n Root -d1 | grep -q 'IOConsoleLocked" = Yes'
}

mkdir -p "$SHOT_DIR" "${LOG:h}"
print "# AC-09 시작 $(date '+%Y-%m-%d %H:%M:%S') kext=$(kmutil showloaded 2>/dev/null | awk '/local.virtiovideo.VirtioVideo /{print $7}')" | tee -a "$LOG"

failures=0
for (( i = 1; i <= CYCLES; i++ )); do
    sleeps_before=$(counter VVDisplayPower sleeps)
    wakes_before=$(counter VVDisplayPower wakes)
    blanks_before=$(counter VVScanout blanks)
    print "\n[$i/$CYCLES] 디스플레이를 재웁니다. 30초쯤 뒤 키를 눌러 깨우고 로그인하세요."
    sleep 1
    pmset displaysleepnow

    waited=0
    while [[ $(counter VVDisplayPower sleeps) == "$sleeps_before" && $waited -lt $SLEEP_TIMEOUT_S ]]; do
        sleep 1; (( waited++ ))
    done
    blanks_mid=$(counter VVScanout blanks)

    # 깨어남(wakes 증가)과 로그인(잠금 해제)을 기다린다.
    until [[ $(counter VVDisplayPower wakes) != "$wakes_before" ]]; do sleep 1; done
    woke_at=$(date '+%H:%M:%S')
    until ! screen_locked; do sleep 1; done
    sleep $SETTLE_S

    shot="$SHOT_DIR/display-sleep-$(printf '%02d' $i).png"
    screencapture -x "$shot"
    size=$(sips -g pixelWidth -g pixelHeight "$shot" 2>/dev/null | awk '/pixel/{printf "%s ", $2}')

    errors=$(counter VVScanout flush-errors)
    result=ok
    [[ $blanks_mid == "$blanks_before" ]] && result="fail:no-blank"
    [[ $errors != 0 ]] && result="fail:flush-errors=$errors"
    [[ $result != ok ]] && (( failures++ ))
    print "cycle=$i result=$result woke=$woke_at sleeps=$(counter VVDisplayPower sleeps) wakes=$(counter VVDisplayPower wakes) blanks=$(counter VVScanout blanks) flush-errors=$errors shot=${shot:t} size=${size% }" | tee -a "$LOG"
done

print "# AC-09 끝 $(date '+%H:%M:%S') failures=$failures" | tee -a "$LOG"
exit $(( failures > 0 ))
