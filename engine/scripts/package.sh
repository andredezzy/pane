#!/usr/bin/env bash
# Assembles app/build/Pane.app: the engine's bundle (PaneCore.framework,
# helpers, resources, Info.plist) with the Swift app's executable in place of
# Chrome's launcher, then signs the nested code inside-out.
# Usage: package.sh   (PANE_ENGINE_OUT overrides the engine build dir;
# PANE_STRIP=1 strips the engine's local symbols, as dist.sh does;
# PANE_SIGN_IDENTITY signs with a Developer ID instead of ad-hoc;
# PANE_ENGINE_ARCHIVE=1 also archives the engine for prebuilt-engines.tsv)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
APP_DIR="$ROOT_DIR/app"
ENGINE_OUT="${PANE_ENGINE_OUT:-$HOME/chromium/src/out/Release}"
SWIFT_APP="$APP_DIR/build/DerivedData/Build/Products/Release/Pane.app"
# PANE_APP_OUT puts it elsewhere, beside a build that is running.
OUT="${PANE_APP_OUT:-$APP_DIR/build/Pane.app}"

# An archive is named by the engine key of this checkout's engine sources,
# and CI and every release build on it: it must be the engine built from
# them. build.sh records the key it built (pane-engine.key); an engine built
# before the last change to those sources, or from a checkout whose patches
# or //pane were not these, has another key, or none. Checked first, before
# the app is built.
if [ "${PANE_ENGINE_ARCHIVE:-}" = 1 ]; then
	KEY="$("$ROOT_DIR/engine/scripts/engine-key.sh")"
	BUILT_KEY="$(cat "$ENGINE_OUT/pane-engine.key" 2>/dev/null || true)"

	if [ -z "$BUILT_KEY" ]; then
		echo "error: $ENGINE_OUT records no engine key: build it with build.sh before archiving it" >&2
		exit 1
	elif [ "$BUILT_KEY" != "$KEY" ]; then
		echo "error: the engine in $ENGINE_OUT was built from other sources (key $BUILT_KEY) than this checkout's ($KEY): build it again (build.sh)" >&2
		exit 1
	fi
fi

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

# The Swift app's minimum macOS (project.yml), over Chromium's older one: the
# executable does not start on an older macOS, and Finder says so only when
# the bundle names it.
plutil -replace LSMinimumSystemVersion -string \
	"$(plutil -extract LSMinimumSystemVersion raw "$SWIFT_APP/Contents/Info.plist")" "$PLIST"

plutil -replace CFBundleIconFile -string AppIcon "$PLIST"
rm -f "$OUT/Contents/Resources/app.icns"

FRAMEWORK="$OUT/Contents/Frameworks/PaneCore.framework"
VERSION_DIR="$FRAMEWORK/Versions/Current"

# Drops local symbols. strip warns, for each file, that its signature no
# longer holds (the bundle is signed again after); only that is silenced, so
# a wrong path or a file that is no Mach-O still says so.
strip_local() {
	strip -x "$@" 2> >(grep -v 'will invalidate the code signature' >&2)
}

# The notification helpers post Pane's notifications under their own icon:
# Pane's too.
for helper in "$VERSION_DIR"/Helpers/*Alerts*.app; do
	cp "$OUT/Contents/Resources/Assets.car" "$OUT/Contents/Resources/AppIcon.icns" "$helper/Contents/Resources/"
	plutil -replace CFBundleIconFile -string AppIcon "$helper/Contents/Info.plist"
	rm -f "$helper/Contents/Resources/app.icns"
done

# A release (dist.sh) drops the engine's local symbols, as Chrome's official
# builds do: PaneCore shrinks from ~480 MB to ~270 MB, with its exports, which
# the app links, unchanged. Development builds keep them for stack traces.
if [ "${PANE_STRIP:-}" = 1 ]; then
	echo "== strip"
	strip_local "$VERSION_DIR/PaneCore" "$VERSION_DIR"/Libraries/*.dylib
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

# ditto kept the engine bundle's dates, and macOS caches an app's icon by its
# path and date: without a new date the Dock and notifications kept showing
# the icon a bundle at this path had before (Chromium's). The build's date,
# and LaunchServices told at once.
find "$OUT" -name "*.app" -exec touch {} +
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$OUT"

echo "packaged $OUT"

# The engine a release was built with, for the app to build on without a
# Chromium checkout (fetch-engine.sh): app/release/PaneCore-<chromium>-<key>.zip,
# the engine's bundle stripped, with PaneCore.framework at its top for the
# linker, and its SHA-256. A GitHub release carries it, and
# prebuilt-engines.tsv names that release.
if [ "${PANE_ENGINE_ARCHIVE:-}" = 1 ]; then
	echo "== engine archive"
	KEY="$("$ROOT_DIR/engine/scripts/engine-key.sh")"
	CHROMIUM="$(tr -d '[:space:]' < "$ROOT_DIR/engine/CHROMIUM_VERSION")"
	BUILT="$(plutil -extract CFBundleShortVersionString raw "$ENGINE_OUT/Pane.app/Contents/Info.plist")"

	if [ "$BUILT" != "$CHROMIUM" ]; then
		echo "error: the engine in $ENGINE_OUT is $BUILT, not $CHROMIUM" >&2
		exit 1
	fi

	ARCHIVE="$APP_DIR/release/PaneCore-$CHROMIUM-${KEY:0:12}.zip"
	STAGE="$(mktemp -d)"
	ditto "$ENGINE_OUT/Pane.app" "$STAGE/Pane.app"
	ln -s Pane.app/Contents/Frameworks/PaneCore.framework "$STAGE/PaneCore.framework"
	strip_local "$STAGE/PaneCore.framework/Versions/Current/PaneCore" "$STAGE/PaneCore.framework/Versions/Current"/Libraries/*.dylib

	mkdir -p "$APP_DIR/release"
	rm -f "$ARCHIVE"
	ditto -c -k --norsrc --noextattr --noacl "$STAGE" "$ARCHIVE"
	rm -rf "$STAGE"

	SHA256="$(shasum -a 256 "$ARCHIVE" | awk '{ print $1 }')"
	echo "$SHA256  $(basename "$ARCHIVE")" > "$ARCHIVE.sha256"
	echo "archived $ARCHIVE"
	printf 'prebuilt-engines.tsv: %s\tengine-%s\t%s\n' "$KEY" "${KEY:0:12}" "$SHA256"
fi
