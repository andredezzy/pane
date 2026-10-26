#!/usr/bin/env bash
# Assembles native/app/build/Pane.app: the engine's bundle (PaneCore.framework,
# helpers, resources, Info.plist) with the Swift app's executable in place of
# Chrome's launcher, then ad-hoc signs the nested code inside-out.
# Usage: package.sh   (PANE_ENGINE_OUT overrides the engine build dir)
set -euo pipefail

NATIVE_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
APP_DIR="$NATIVE_DIR/app"
ENGINE_OUT="${PANE_ENGINE_OUT:-$HOME/chromium/src/out/Release}"
SWIFT_APP="$APP_DIR/build/DerivedData/Build/Products/Release/Pane.app"
OUT="$APP_DIR/build/Pane.app"

echo "== swift app"
(cd "$APP_DIR" && xcodegen generate --quiet)
xcodebuild -project "$APP_DIR/Pane.xcodeproj" -scheme Pane -configuration Release \
	-derivedDataPath "$APP_DIR/build/DerivedData" PANE_ENGINE_OUT="$ENGINE_OUT" \
	-quiet build

echo "== assemble"
rm -rf "$OUT"
ditto "$ENGINE_OUT/Pane.app" "$OUT"
cp "$SWIFT_APP/Contents/MacOS/Pane" "$OUT/Contents/MacOS/Pane"

if [ -d "$SWIFT_APP/Contents/Resources" ]; then
	ditto "$SWIFT_APP/Contents/Resources" "$OUT/Contents/Resources"
fi

echo "== sign"
sign() {
	codesign --force --sign - --timestamp=none "$1"
}

FRAMEWORK="$OUT/Contents/Frameworks/PaneCore.framework"
VERSION_DIR="$FRAMEWORK/Versions/Current"

for helper in "$VERSION_DIR"/Helpers/*; do
	sign "$helper"
done

for library in "$VERSION_DIR"/Libraries/*.dylib; do
	sign "$library"
done

sign "$FRAMEWORK"
sign "$OUT"
codesign --verify --deep --strict "$OUT"

echo "packaged $OUT"
