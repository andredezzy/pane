#!/usr/bin/env bash
# Assembles native/app/build/Pane.app: the engine's bundle (PaneCore.framework,
# helpers, resources, Info.plist) with the Swift app's executable in place of
# Chrome's launcher, then ad-hoc signs the nested code inside-out.
# Usage: package.sh   (PANE_ENGINE_OUT overrides the engine build dir;
# PANE_STRIP=1 strips the engine's local symbols, as dist.sh does)
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

# Pane's identity over Chromium's: its version (app/VERSION, the one version
# source), which Finder, the About panel, Settings, and the update check
# show, and its icon (the Swift app's asset catalog, which replaced
# Chromium's Assets.car above).
PANE_VERSION="$(tr -d '[:space:]' < "$APP_DIR/VERSION")"
PLIST="$OUT/Contents/Info.plist"

for key in PaneVersion CFBundleShortVersionString CFBundleVersion; do
	plutil -replace "$key" -string "$PANE_VERSION" "$PLIST"
done

plutil -replace CFBundleIconFile -string AppIcon "$PLIST"
rm -f "$OUT/Contents/Resources/app.icns"

FRAMEWORK="$OUT/Contents/Frameworks/PaneCore.framework"
VERSION_DIR="$FRAMEWORK/Versions/Current"

# A release (dist.sh) drops the engine's local symbols, as Chrome's official
# builds do: PaneCore shrinks from ~480 MB to ~270 MB, with its exports, which
# the app links, unchanged. Development builds keep them for stack traces.
if [ "${PANE_STRIP:-}" = 1 ]; then
	echo "== strip"
	# strip warns that the old signatures no longer hold; they are redone below.
	strip -x "$VERSION_DIR/PaneCore" "$VERSION_DIR"/Libraries/*.dylib 2>/dev/null
fi

echo "== sign"
sign() {
	codesign --force --sign - --timestamp=none "$1"
}

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
