#!/bin/zsh
# VirtioVGA 설치 패키지를 만듭니다. 결과: build/pkg/VirtioVGA-<버전>.pkg (서명 없음)
#
# 패키지에 들어가는 것:
#   /Library/Extensions/VirtioVGA.kext                         본 드라이버 (postinstall이 kmutil load -z -p로 등록)
#   /Library/Application Support/VirtioVGA/
#       VirtioVGANdrvBlocker.kext   OpenCore에 넣을 보조 kext (opencore-setup.sh가 EFI로 복사)
#       opencore-setup.sh           OpenCore config.plist 설정 (사용자가 sudo로 직접 실행)
#       uninstall.sh                제거
#       vvstat, vvresize            상태 조회 도구, 창 크기 자동 전환 에이전트
#   /Library/LaunchAgents/local.virtiovga.vvresize.plist        에이전트 자동 시작
#
# 사용법: packaging/build-pkg.sh   (release 빌드를 새로 만듭니다)
set -eu
readonly ROOT=${0:A:h:h}
readonly PKG_DIR="$ROOT/build/pkg"
readonly STAGE="$PKG_DIR/root"
readonly SUPPORT="$STAGE/Library/Application Support/VirtioVGA"
readonly IDENTIFIER=local.virtiovga.pkg
readonly MIN_MACOS=15.0

version=$(/usr/libexec/PlistBuddy -c "Print :CFBundleVersion" "$ROOT/kext/Info.plist")
readonly VERSION=$version
readonly COMPONENT="$PKG_DIR/VirtioVGA-component.pkg"
readonly PRODUCT="$PKG_DIR/VirtioVGA-$VERSION.pkg"

"$ROOT/scripts/build.sh" release
"$ROOT/scripts/test.sh"

rm -rf "$PKG_DIR"
mkdir -p "$STAGE/Library/Extensions" "$SUPPORT" "$STAGE/Library/LaunchAgents"
ditto --norsrc --noextattr "$ROOT/build/VirtioVGA.kext" "$STAGE/Library/Extensions/VirtioVGA.kext"
ditto --norsrc --noextattr "$ROOT/build/VirtioVGANdrvBlocker.kext" "$SUPPORT/VirtioVGANdrvBlocker.kext"
install -m 755 "$ROOT/packaging/opencore-setup.sh" "$SUPPORT/opencore-setup.sh"
install -m 755 "$ROOT/packaging/uninstall.sh" "$SUPPORT/uninstall.sh"
clang -Wall -Wextra -Werror -O2 -mmacosx-version-min=$MIN_MACOS -framework IOKit -framework CoreFoundation \
    -o "$SUPPORT/vvstat" "$ROOT/tools/vvstat.c"
swiftc -O -target x86_64-apple-macos$MIN_MACOS -o "$SUPPORT/vvresize" "$ROOT/tools/vvresize.swift"
install -m 644 "$ROOT/packaging/resources/local.virtiovga.vvresize.plist" "$STAGE/Library/LaunchAgents/"
plutil -lint "$STAGE/Library/LaunchAgents/local.virtiovga.vvresize.plist"

# 확장 속성(com.apple.provenance 등)이 남으면 pkg에 ._ 파일(AppleDouble)이 함께 들어간다(0.9.0 첫 빌드에서 확인).
xattr -cr "$STAGE"
export COPYFILE_DISABLE=1

# kext 번들을 "옮길 수 있음"으로 두면 같은 번들이 다른 곳에 있을 때 설치 프로그램이 그곳에 덮어씁니다. 항상 정한 위치에 둡니다.
readonly COMPONENT_PLIST="$PKG_DIR/components.plist"
pkgbuild --analyze --root "$STAGE" "$COMPONENT_PLIST"
count=$(/usr/libexec/PlistBuddy -c "Print" "$COMPONENT_PLIST" | grep -c "RootRelativeBundlePath" || true)
for (( i = 0; i < count; i++ )); do
    /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$COMPONENT_PLIST" 2>/dev/null || true
    /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$COMPONENT_PLIST"
done
pkgbuild --root "$STAGE" --component-plist "$COMPONENT_PLIST" --identifier "$IDENTIFIER" --version "$VERSION" \
    --install-location / --scripts "$ROOT/packaging/scripts" --ownership recommended "$COMPONENT"

# 배포 정의: 제목, macOS 15 이상, Intel(x86_64)만.
readonly DISTRIBUTION="$PKG_DIR/distribution.xml"
cat > "$DISTRIBUTION" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>VirtioVGA $VERSION</title>
    <options customize="never" require-scripts="true" hostArchitectures="x86_64"/>
    <allowed-os-versions><os-version min="$MIN_MACOS"/></allowed-os-versions>
    <welcome file="welcome.txt"/>
    <choices-outline><line choice="default"><line choice="$IDENTIFIER"/></line></choices-outline>
    <choice id="default"/>
    <choice id="$IDENTIFIER" visible="false"><pkg-ref id="$IDENTIFIER"/></choice>
    <pkg-ref id="$IDENTIFIER" version="$VERSION" onConclusion="RequireRestart">VirtioVGA-component.pkg</pkg-ref>
</installer-gui-script>
EOF
productbuild --distribution "$DISTRIBUTION" --resources "$ROOT/packaging/resources" --package-path "$PKG_DIR" "$PRODUCT"
rm -f "$COMPONENT"
# 내려받은 사람이 같은 폴더에서 `shasum -a 256 -c`로 확인할 수 있게 파일 이름만 적는다(절대 경로를 넣지 않는다).
(cd "$PKG_DIR" && shasum -a 256 "${PRODUCT:t}") | tee "$PRODUCT.sha256"
print "built $PRODUCT"
