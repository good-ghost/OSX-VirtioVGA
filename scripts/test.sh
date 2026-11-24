#!/usr/bin/env bash
# kext/include/의 커널 비의존 코드를 사용자 공간에서 빌드해 단위 테스트를 돌린다 (WI-0.006, NFR-010).
# 종료 코드: 0 = 모두 통과, 그 외 = 컴파일 실패 또는 테스트 실패.
set -euo pipefail

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly OUT_DIR="$ROOT/build/tests"
readonly BINARY="$OUT_DIR/vv-tests"

mkdir -p "$OUT_DIR"

CLANGXX="$(xcrun --find clang++)"
SDK="$(xcrun --sdk macosx --show-sdk-path)"
readonly CLANGXX SDK

"$CLANGXX" -isysroot "$SDK" -std=c++17 -Wall -Wextra -Werror -g -O0 \
  -fsanitize=address,undefined \
  -I "$ROOT/kext/include" -I "$ROOT/tests" \
  "$ROOT"/tests/*.cpp \
  -o "$BINARY"

"$BINARY"
