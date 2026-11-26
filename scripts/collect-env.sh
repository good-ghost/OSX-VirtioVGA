#!/usr/bin/env bash
# 게스트(macOS) 디스플레이 환경을 읽기 전용으로 수집한다.
# 왜: 드라이버 적용 전/후를 같은 기준으로 비교하기 위해서다 (REQUIREMENT.md §2, §9, REQ-075).
# sudo가 필요 없는 명령만 쓴다.
#
# 사용법:
#   scripts/collect-env.sh                 표준 출력으로 내보낸다
#   scripts/collect-env.sh --save <label>  docs/research/env-<YYYYMMDD-HHMMSS>-<label>.txt에도 저장한다
# 비교: scripts/compare-env.sh <이전 파일> <이후 파일>
set -euo pipefail

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly RESEARCH_DIR="$ROOT/docs/research"
readonly INFO_PLIST="$ROOT/kext/Info.plist"
readonly VIRTIO_VENDOR_ID="1af4"
readonly VIRTIO_GPU_DEVICE_ID="1050"
readonly BUNDLE_ID_PATTERN="virtiovga"
readonly LINE_WIDTH=240

usage() {
  echo "usage: $0 [--save <label>]" >&2
  exit 2
}

save_label=""
case "${1:-}" in
  "") ;;
  --save)
    [ -n "${2:-}" ] || usage
    # 파일 이름에 쓰므로 영문·숫자·-·_만 허용한다.
    [[ "$2" =~ ^[A-Za-z0-9_-]+$ ]] || { echo "label은 영문·숫자·-·_만 쓸 수 있다: $2" >&2; exit 2; }
    save_label="$2" ;;
  *) usage ;;
esac

section() { printf '\n===== %s =====\n' "$1"; }

# grep이 아무것도 찾지 못해도(종료 코드 1) 수집을 계속한다. 실제 오류(2 이상)는 그대로 올린다.
filter() {
  grep "$@" || [ $? -eq 1 ]
}

# Info.plist의 IOKitPersonalities에서 IOClass 목록을 읽는다.
driver_classes() {
  /usr/libexec/PlistBuddy -c 'Print :IOKitPersonalities' "$INFO_PLIST" 2>/dev/null \
    | awk -F' = ' '/IOClass = / { print $2 }' | sort -u
}

collect() {
  echo "collected-at: $(date '+%Y-%m-%d %H:%M:%S %z')"
  echo "uptime: $(uptime)"

  section "OS"
  sw_vers
  uname -m
  sysctl -n hw.model hw.memsize machdep.cpu.brand_string

  section "SIP / NVRAM"
  csrutil status
  nvram boot-args 2>/dev/null || echo "boot-args: (없음)"
  nvram csr-active-config 2>/dev/null || echo "csr-active-config: (없음)"

  section "Displays (system_profiler)"
  system_profiler SPDisplaysDataType

  section "IOFramebuffer 서비스"
  ioreg -r -c IOFramebuffer -l -w0 \
    | filter -E '\+-o|"(IOClass|IOFBMemorySize|IONameMatch|IOProbeScore|IOMatchCategory|IOFBConfig)"' \
    | cut -c1-"$LINE_WIDTH"

  section "VirtIO GPU PCI 장치 (${VIRTIO_VENDOR_ID}:${VIRTIO_GPU_DEVICE_ID})"
  ioreg -r -c IOPCIDevice -l -w0 \
    | filter -E '"(IOName|pcidebug|vendor-id|device-id|class-code|subsystem-id|assigned-addresses)"' \
    | cut -c1-"$LINE_WIDTH"

  section "VirtIO / 그래픽 관련 로드된 kext"
  kmutil showloaded 2>/dev/null \
    | filter -iE "virtio|IOGraphics|IONDRV|Lilu|WhateverGreen|$BUNDLE_ID_PATTERN" \
    | cut -c1-200

  section "VirtioVGA 드라이버 서비스 (Info.plist의 IOClass별)"
  local classes
  classes="$(driver_classes)"
  if [ -z "$classes" ]; then
    echo "(Info.plist에 IOKitPersonalities 없음)"
  fi
  local class
  for class in $classes; do
    echo "-- $class"
    local dump
    dump="$(ioreg -r -c "$class" -l -w0 -d1)"
    if [ -z "$dump" ]; then
      echo "(객체 없음)"
      continue
    fi
    # 서비스 줄과 이 드라이버가 공개하는 VV* 속성만 남긴다.
    printf '%s\n' "$dump" | filter -E '\+-o|"VV' | cut -c1-2000
  done

  section "Xcode / SDK"
  xcode-select -p
  xcodebuild -version
  xcrun --sdk macosx --show-sdk-path
  ls /Library/Developer/KDKs 2>/dev/null || echo "KDK: 설치되지 않음"
}

if [ -z "$save_label" ]; then
  collect
else
  mkdir -p "$RESEARCH_DIR"
  out="$RESEARCH_DIR/env-$(date '+%Y%m%d-%H%M%S')-$save_label.txt"
  collect | tee "$out"
  echo "saved: $out" >&2
fi
