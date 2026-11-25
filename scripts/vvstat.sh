#!/bin/zsh
# 상태 조회 도구 tools/vvstat.c를 build/에 만들고 실행한다 (WI-5.008). 사용법: scripts/vvstat.sh [--log N] [--all]
# IORegistry 속성만 읽으므로 root가 필요 없다.
set -eu
readonly ROOT=${0:A:h:h}
readonly SRC="$ROOT/tools/vvstat.c"
readonly BIN="$ROOT/build/vvstat"
if [[ ! -x "$BIN" || "$SRC" -nt "$BIN" ]]; then
    mkdir -p "${BIN:h}"
    clang -Wall -Wextra -Werror -O2 -framework IOKit -framework CoreFoundation -o "$BIN" "$SRC"
fi
exec "$BIN" "$@"
