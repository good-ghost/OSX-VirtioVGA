#!/usr/bin/env bash
# kext 두 개를 빌드한다.
#   build/VirtioVGA.kext               본 드라이버 (kext/, 보조 kernel collection으로 설치)
#   build/VirtioVGANdrvBlocker.kext    IONDRV 차단 보조 kext (kext-helper/, OpenCore로 주입)
# 왜 Xcode 프로젝트가 아닌가: .xcodeproj를 손으로 관리하지 않고 CLI만으로 재현 가능하게 빌드하기 위해서다
# (Xcode 26.3에 kext 템플릿은 있다 — PHASE-PLAN-00 결정 기록 참고).
#
# 사용법: scripts/build.sh [debug|release]   (기본 debug)
set -euo pipefail

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly CONFIG="${1:-debug}"
readonly ARCH="x86_64"
readonly DEPLOYMENT_TARGET="15.0"   # REQUIREMENT NFR-008: macOS 15.x

readonly MAIN_SRC_DIR="$ROOT/kext/src"
readonly INC_DIR="$ROOT/kext/include"
readonly BUILD_DIR="$ROOT/build"
readonly DSYM_ARCHIVE="$BUILD_DIR/dsym-archive"

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
  # IOPCIDevice 등 kext API는 "Use PCIDriverKit"로 deprecated 표시돼 있다. 그러나 디스플레이 드라이버는
  # DriverKit으로 만들 수 없어(REQUIREMENT §7) kext API를 써야 한다. 경고는 보이게 두고 오류로만 만들지 않는다.
  -Wno-error=deprecated-declarations
  -I "$INC_DIR"
  -I "$MAIN_SRC_DIR"
  -isystem "$KERNEL_HEADERS"
  "${OPT_FLAGS[@]}"
)
CXX_FLAGS=(-std=c++17 -fapple-kext -fno-exceptions -fno-rtti)

# build_kext <이름> <Info.plist> <소스 파일...>
# Info.plist가 bundle id와 버전의 유일한 출처다. kmod_info(KmodInfo.c)는 kext마다 그 값으로 따로 컴파일한다.
build_kext() {
  local name="$1" info_plist="$2"
  shift 2
  local bundle="$BUILD_DIR/$name.kext"
  local obj_dir="$BUILD_DIR/obj/$CONFIG/$name"
  local bundle_id version
  bundle_id="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$info_plist")"
  version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$info_plist")"

  rm -rf "$obj_dir" "$bundle"
  mkdir -p "$obj_dir" "$bundle/Contents/MacOS"

  local objects=() src obj
  for src in "$@"; do
    obj="$obj_dir/$(basename "$src").o"
    case "$src" in
      *.c)
        "$CLANG" "${COMMON_FLAGS[@]}" -std=c11 \
          -DVV_BUNDLE_ID_STR="\"$bundle_id\"" -DVV_VERSION_STR="\"$version\"" \
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
    -o "$bundle/Contents/MacOS/$name"

  cp "$info_plist" "$bundle/Contents/Info.plist"

  # 패닉 백트레이스를 소스 줄로 바꾸려면(atos) 디버그 심볼이 필요하다 (WI-0.004, docs/DEBUGGING.md).
  # dSYM은 kext 번들 밖에 둔다. 설치할 때는 번들만 복사한다.
  local dsym="$BUILD_DIR/$name.kext.dSYM"
  rm -rf "$dsym"
  "$(xcrun --find dsymutil)" "$bundle/Contents/MacOS/$name" -o "$dsym"

  # 다시 빌드해도 예전 패닉을 해석할 수 있도록 UUID별로 보관한다. scripts/panic-summary.py가 UUID로 찾는다.
  local uuid
  uuid="$(dwarfdump --uuid "$bundle/Contents/MacOS/$name" | awk '/x86_64/ { print $2; exit }')"
  mkdir -p "$DSYM_ARCHIVE"
  rm -rf "$DSYM_ARCHIVE/$uuid.dSYM"
  cp -R "$dsym" "$DSYM_ARCHIVE/$uuid.dSYM"

  # 코드가 쓰는 KPI가 Info.plist의 OSBundleLibraries에 모두 선언됐는지 확인한다.
  # 빠지면 빌드는 되지만 부팅 때 심볼 해석에 실패할 수 있다 (WI-1.004에서 kpi.mach 누락 발견).
  local required_libraries library missing=""
  required_libraries="$(kmutil libraries -p "$bundle" --xml 2>/dev/null \
    | plutil -extract OSBundleLibraries xml1 -o - - \
    | awk -F'[<>]' '/<key>/ { print $3 }')"
  for library in $required_libraries; do
    # Apple 내부용 KPI는 서드파티 kext가 쓸 수 없다 (WI-1.006에서 thread_call_cancel_wait로 발견).
    if [ "$library" = "com.apple.kpi.private" ]; then
      echo "$name: Apple 내부용 KPI(com.apple.kpi.private)를 쓰는 심볼이 있다. kmutil libraries -p로 찾아 공개 API로 바꾼다" >&2
      exit 1
    fi
    /usr/libexec/PlistBuddy -c "Print :OSBundleLibraries:$library" "$info_plist" >/dev/null 2>&1 || missing="$missing $library"
  done
  if [ -n "$missing" ]; then
    echo "$name: Info.plist OSBundleLibraries에 선언되지 않은 라이브러리:$missing" >&2
    exit 1
  fi

  echo "built $bundle ($bundle_id $version, $CONFIG, UUID $uuid)"
}

main_sources=()
for src in "$MAIN_SRC_DIR"/*.c "$MAIN_SRC_DIR"/*.cpp; do
  [ -e "$src" ] && main_sources+=("$src")
done
build_kext VirtioVGA "$ROOT/kext/Info.plist" "${main_sources[@]}"

# 보조 kext는 IOGraphicsFamily에 의존하면 안 된다 (OpenCore 주입, T-04). 본 kext에서 kmod_info와 boot-arg 해석만 가져온다.
build_kext VirtioVGANdrvBlocker "$ROOT/kext-helper/Info.plist" \
  "$MAIN_SRC_DIR/KmodInfo.c" "$MAIN_SRC_DIR/VVBootArgs.cpp" "$ROOT"/kext-helper/src/*.cpp
