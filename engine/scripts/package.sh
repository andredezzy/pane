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

# The licenses that go with every copy: Pane's own (GPL-3.0), NOTICE, and the
# license of each project Pane takes code from (third_party), laid out as in
# the repo. Chromium's components credit themselves at chrome://credits.
LICENSES="$OUT/Contents/Resources/Licenses"
mkdir -p "$LICENSES"
cp "$ROOT_DIR/LICENSE" "$ROOT_DIR/NOTICE" "$LICENSES/"
ditto "$ROOT_DIR/third_party" "$LICENSES/third_party"

# Chrome's CRLSet, which the engine hands its certificate verifier at start
# (ADR 0111): fetched as the app is built, with a day's copy kept in
# app/build for builds without the network. dist.sh requires it.
CRLSET="$APP_DIR/build/crl-set"

if [ -z "$(find "$CRLSET" -mtime -1 2>/dev/null)" ]; then
	if "$ROOT_DIR/engine/scripts/fetch-crlset.sh" "$CRLSET.new"; then
		mv "$CRLSET.new" "$CRLSET"
	else
		rm -f "$CRLSET.new"
		echo "warning: no new CRLSet: the app carries the last one fetched, if any" >&2
	fi
fi

if [ -f "$CRLSET" ]; then
	cp "$CRLSET" "$OUT/Contents/Resources/crl-set"
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

# Pane's name where macOS shows the bundle's own words, in every language:
# the permission prompts (camera, microphone, location, Bluetooth, passkeys),
# which Chromium's strings word "Once Chromium has access…", and the About
# panel's copyright. The commit it was built from, and its engine's key (the
# one build.sh recorded, or else this checkout's, as a prebuilt engine has
# none), go beside the version.
# Cast is off, so its Bonjour service goes.
python3 -I - "$OUT/Contents/Resources" "$PANE_VERSION" <<'PY'
import pathlib, re, sys
resources, version = pathlib.Path(sys.argv[1]), sys.argv[2]
fixed = {
    "NSHumanReadableCopyright": "Copyright 2026 Pane. Built on Chromium, copyright The Chromium Authors.",
    "CFBundleGetInfoString": f"Pane {version}, built on Chromium.",
}
for strings in resources.glob("*.lproj/InfoPlist.strings"):
    raw = strings.read_bytes()
    encoding = "utf-16" if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else "utf-8"
    # A key is quoted or bare: "Chromium Shortcut" = …; or CFBundleName = …;
    def line(match):
        quoted, bare, value = match.groups()
        key, written = (quoted, f'"{quoted}"') if quoted is not None else (bare, bare)
        value = fixed.get(key, value.replace("Chromium", "Pane"))
        return f'{written} = "{value}";'
    text = re.sub(r'^(?:"([^"]+)"|([A-Za-z0-9_.]+)) = "((?:[^"\\]|\\.)*)";', line, raw.decode(encoding), flags=re.M)
    strings.write_bytes(text.encode(encoding))
PY
plutil -replace PaneCommit -string "$(git -C "$ROOT_DIR" rev-parse HEAD)" "$PLIST"
ENGINE_KEY="$(cat "$ENGINE_OUT/pane-engine.key" 2>/dev/null || "$ROOT_DIR/engine/scripts/engine-key.sh" --worktree)"
plutil -replace PaneEngineKey -string "$ENGINE_KEY" "$PLIST"
plutil -remove NSBonjourServices "$PLIST" 2>/dev/null || true

# Other apps' sign-ins (ASWebAuthenticationSession) stay in macOS's own
# sheet, as Dia's do: Chrome opens them in a popup Browser, which Pane turns
# into a tab of its window, and the sign-in keeps the popup's Browser, gone
# by then, to close its page (ADR 0110).
plutil -remove ASWebAuthenticationSessionWebBrowserSupportCapabilities "$PLIST" 2>/dev/null || true

FRAMEWORK="$OUT/Contents/Frameworks/PaneCore.framework"
VERSION_DIR="$FRAMEWORK/Versions/Current"

# Drops local symbols. strip warns, for each file, that its signature no
# longer holds (the bundle is signed again after); only that is silenced, so
# a wrong path or a file that is no Mach-O still says so.
strip_local() {
	strip -x "$@" 2> >(grep -v 'will invalidate the code signature' >&2)
}

# The notification helpers post Pane's notifications under their own icon:
# Pane's too, from a catalog of the icon alone (3 MB), not the app's, whose
# skies for the Dock would add 4.5 MB to each.
ICON_ONLY="$(mktemp -d)"
xcrun actool "$APP_DIR/Pane/AppIcon.icon" --compile "$ICON_ONLY" --platform macosx \
	--minimum-deployment-target "$(plutil -extract LSMinimumSystemVersion raw "$PLIST")" \
	--app-icon AppIcon --output-partial-info-plist "$ICON_ONLY/partial.plist" \
	--output-format human-readable-text --errors > /dev/null

for helper in "$VERSION_DIR"/Helpers/*Alerts*.app; do
	cp "$ICON_ONLY/Assets.car" "$ICON_ONLY/AppIcon.icns" "$helper/Contents/Resources/"
	plutil -replace CFBundleIconFile -string AppIcon "$helper/Contents/Info.plist"
	plutil -replace CFBundleIconName -string AppIcon "$helper/Contents/Info.plist"
	rm -f "$helper/Contents/Resources/app.icns"
done
rm -rf "$ICON_ONLY"

# A release (dist.sh) drops the engine's local symbols, as Chrome's official
# builds do: PaneCore shrinks from ~480 MB to ~270 MB, with its exports, which
# the app links, unchanged. Development builds keep them for stack traces.
if [ "${PANE_STRIP:-}" = 1 ]; then
	echo "== strip"
	strip_local "$VERSION_DIR/PaneCore" "$VERSION_DIR"/Libraries/*.dylib \
		"$OUT/Contents/MacOS/Pane" "$VERSION_DIR/Helpers/chrome_crashpad_handler" \
		"$VERSION_DIR/Helpers/app_mode_loader" "$VERSION_DIR"/Helpers/*.app/Contents/MacOS/*
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
