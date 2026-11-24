#!/usr/bin/env bash
# VirtioVideo.kext를 빌드한다. 결과: build/VirtioVideo.kext
# 왜 Xcode 프로젝트가 아닌가: .xcodeproj를 손으로 관리하지 않고 CLI만으로 재현 가능하게 빌드하기 위해서다
# (Xcode 26.3에 kext 템플릿은 있다 — PHASE-PLAN-00 결정 기록 참고).
#
# 사용법: scripts/build.sh [debug|release]   (기본 debug)
set -euo pipefail

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly CONFIG="${1:-debug}"
readonly KEXT_NAME="VirtioVideo"
readonly ARCH="x86_64"
readonly DEPLOYMENT_TARGET="15.0"   # REQUIREMENT NFR-008: macOS 15.x

readonly SRC_DIR="$ROOT/kext/src"
readonly INC_DIR="$ROOT/kext/include"
readonly INFO_PLIST="$ROOT/kext/Info.plist"
readonly BUILD_DIR="$ROOT/build"
readonly OBJ_DIR="$BUILD_DIR/obj/$CONFIG"
readonly BUNDLE="$BUILD_DIR/$KEXT_NAME.kext"

case "$CONFIG" in
  debug)   OPT_FLAGS=(-O0 -g -DVV_DEBUG=1) ;;
  release) OPT_FLAGS=(-Os -g) ;;
  *) echo "unknown config: $CONFIG (debug|release)" >&2; exit 2 ;;
esac

SDK="$(xcrun --sdk macosx --show-sdk-path)"
readonly SDK
readonly KERNEL_HEADERS="$SDK/System/Library/Frameworks/Kernel.framework/Headers"
CLANG="$(xcrun --find clang)"
CLANGXX="$(xcrun --find clang++)"
readonly CLANG CLANGXX

# Info.plist가 bundle id와 버전의 유일한 출처다.
BUNDLE_ID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$INFO_PLIST")"
VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$INFO_PLIST")"
readonly BUNDLE_ID VERSION

COMMON_FLAGS=(
  -arch "$ARCH"
  -mmacosx-version-min="$DEPLOYMENT_TARGET"
  -isysroot "$SDK"
  -mkernel
  -nostdinc
  -fno-builtin
  -fno-common
  -DKERNEL
  -Wall -Wextra -Werror
  -I "$INC_DIR"
  -isystem "$KERNEL_HEADERS"
  "${OPT_FLAGS[@]}"
)
CXX_FLAGS=(-std=c++17 -fapple-kext -fno-exceptions -fno-rtti)

rm -rf "$OBJ_DIR" "$BUNDLE"
mkdir -p "$OBJ_DIR" "$BUNDLE/Contents/MacOS"

objects=()
for src in "$SRC_DIR"/*.c "$SRC_DIR"/*.cpp; do
  [ -e "$src" ] || continue
  obj="$OBJ_DIR/$(basename "$src").o"
  case "$src" in
    *.c)
      "$CLANG" "${COMMON_FLAGS[@]}" -std=c11 \
        -DVV_BUNDLE_ID_STR="\"$BUNDLE_ID\"" -DVV_VERSION_STR="\"$VERSION\"" \
        -c "$src" -o "$obj" ;;
    *.cpp)
      "$CLANGXX" "${COMMON_FLAGS[@]}" "${CXX_FLAGS[@]}" -c "$src" -o "$obj" ;;
  esac
  objects+=("$obj")
done

"$CLANGXX" -arch "$ARCH" -mmacosx-version-min="$DEPLOYMENT_TARGET" -isysroot "$SDK" \
  -mkernel -nostdlib \
  -Xlinker -kext \
  "${objects[@]}" \
  -lkmodc++ -lkmod -lcc_kext \
  -o "$BUNDLE/Contents/MacOS/$KEXT_NAME"

cp "$INFO_PLIST" "$BUNDLE/Contents/Info.plist"

# 패닉 백트레이스를 소스 줄로 바꾸려면(atos) 디버그 심볼이 필요하다 (WI-0.004, docs/DEBUGGING.md).
# dSYM은 kext 번들 밖에 둔다. EFI에는 번들만 복사한다.
readonly DSYM="$BUILD_DIR/$KEXT_NAME.kext.dSYM"
rm -rf "$DSYM"
"$(xcrun --find dsymutil)" "$BUNDLE/Contents/MacOS/$KEXT_NAME" -o "$DSYM"

# 다시 빌드해도 예전 패닉을 해석할 수 있도록 UUID별로 보관한다. scripts/panic-summary.py가 UUID로 찾는다.
readonly DSYM_ARCHIVE="$BUILD_DIR/dsym-archive"
KEXT_UUID="$(dwarfdump --uuid "$BUNDLE/Contents/MacOS/$KEXT_NAME" | awk '/x86_64/ { print $2; exit }')"
readonly KEXT_UUID
mkdir -p "$DSYM_ARCHIVE"
rm -rf "$DSYM_ARCHIVE/$KEXT_UUID.dSYM"
cp -R "$DSYM" "$DSYM_ARCHIVE/$KEXT_UUID.dSYM"

echo "built $BUNDLE ($BUNDLE_ID $VERSION, $CONFIG)"
echo "dsym  $DSYM"
