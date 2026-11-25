#!/bin/zsh
# vvctl을 build/에 만들고 root로 실행한다 (WI-4.004 측정용). 사용법: scripts/vvctl.sh dirty <0|1> [tile]
# 드라이버의 setProperties는 kIOClientPrivilegeAdministrator(실제로는 root)만 받는다 (0xe00002c1 실측).
set -eu
readonly ROOT=${0:A:h:h}
readonly SRC="$ROOT/scripts/vvctl.c"
readonly BIN="$ROOT/build/vvctl"
if [[ ! -x "$BIN" || "$SRC" -nt "$BIN" ]]; then
    mkdir -p "${BIN:h}"
    clang -Wall -Wextra -Werror -O2 -framework IOKit -framework CoreFoundation -o "$BIN" "$SRC"
fi
exec sudo "$BIN" "$@"
