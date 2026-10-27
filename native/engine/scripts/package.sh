#!/usr/bin/env bash
# Assembles native/app/build/Pane.app: the engine's bundle (PaneCore.framework,
# helpers, resources, Info.plist) with the Swift app's executable in place of
# Chrome's launcher, then signs the nested code inside-out.
# Usage: package.sh   (PANE_ENGINE_OUT overrides the engine build dir;
# PANE_STRIP=1 strips the engine's local symbols, as dist.sh does;
# PANE_SIGN_IDENTITY signs with a Developer ID instead of ad-hoc)
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

# Ad-hoc by default. With a Developer ID (PANE_SIGN_IDENTITY="Developer ID
# Application: …"), each part is signed the way Chrome signs it
# (chrome/installer/mac/signing/parts.py): the hardened runtime and a secure
# timestamp, library validation everywhere except the renderer and GPU
# helpers, which compile code at run time, and the app's entitlements for
# what pages may ask for.
IDENTITY="${PANE_SIGN_IDENTITY:--}"

echo "== sign ($([ "$IDENTITY" = - ] && echo ad-hoc || echo "$IDENTITY"))"
sign() {
	local path="$1" options="${2:-}" entitlements="${3:-}"

	if [ "$IDENTITY" = - ]; then
		codesign --force --sign - --timestamp=none "$path"
	else
		codesign --force --sign "$IDENTITY" --timestamp \
			${options:+--options "$options"} \
			${entitlements:+--entitlements "$APP_DIR/signing/$entitlements"} \
			"$path"
	fi
}

for helper in "$VERSION_DIR"/Helpers/*; do
	case "$helper" in
	*"Renderer).app" | *"GPU).app") sign "$helper" restrict,kill,runtime helper-jit.entitlements ;;
	*) sign "$helper" restrict,library,runtime,kill ;;
	esac
done

for library in "$VERSION_DIR"/Libraries/*.dylib; do
	sign "$library"
done

sign "$FRAMEWORK"
sign "$OUT" restrict,library,runtime,kill app.entitlements
codesign --verify --deep --strict "$OUT"

echo "packaged $OUT"
