#!/usr/bin/env bash
# collect-env.sh로 저장한 두 결과를 비교한다 (WI-0.007, REQ-075).
# 부팅마다 바뀌는 값(수집 시각, uptime, kext 로드 인덱스·주소, IORegistry 객체 id·retain 수, busy 시간)은
# 지우고 비교한다. 그래야 드라이버 적용으로 생긴 차이만 남는다.
#
# 사용법: scripts/compare-env.sh <before.txt> <after.txt>
# 종료 코드: 0 = 차이 없음, 1 = 차이 있음, 2 = 사용법·파일 오류 (diff와 같다)
set -euo pipefail

if [ $# -ne 2 ]; then
  echo "usage: $0 <before.txt> <after.txt>" >&2
  exit 2
fi
for f in "$1" "$2"; do
  [ -r "$f" ] || { echo "읽을 수 없는 파일: $f" >&2; exit 2; }
done

normalize() {
  sed -E \
    -e '/^collected-at: /d' \
    -e '/^uptime: /d' \
    -e 's/^ *[0-9]+ +[0-9]+ +0x[0-9a-f]+ +0x[0-9a-f]+ +0x[0-9a-f]+ +/<kext> /' \
    -e 's/ <[0-9 ]+>$//' \
    -e 's/id 0x[0-9a-f]+/id <id>/g' \
    -e 's/retain [0-9]+/retain <n>/g' \
    -e 's/busy [0-9]+ \([0-9]+ ms\)/busy <n>/g' \
    -e 's/busy [0-9]+/busy <n>/g' \
    "$1"
}

diff -u --label "$1" --label "$2" <(normalize "$1") <(normalize "$2")
