#!/usr/bin/env bash
# Packages Pane for Windows (W7, Pane's own install): lays out the app's
# folder as an install puts it at %LOCALAPPDATA%\Programs\Pane\Application,
# as packaging/windows/pane.release lists it: the engine's files from its
# build (out/Win, or an unpacked engine archive), Pane's version beside
# chrome.dll (pane-version, from packaging/VERSION, as package.sh writes the
# Mac's PaneVersion, ADR 0044), and Chrome's CRLSet (fetch-crlset.sh, kept a
# day in packaging/build as package.sh keeps it). It refuses a package without
# a CRLSet, as dist.sh does. Then it compresses the folder's contents as
# Chromium's installer archive does (chrome/tools/build/win/
# create_installer_archive.py: 7-Zip's LZMA with BCJ2, its dictionaries and
# streams) into the payload Pane's installer carries:
# packaging/release/Pane-<version>-x64-app.7z.
# Usage: package-win.sh   (PANE_WIN_ENGINE_OUT names the engine's folder,
# ~/chromium/src/out/Win by default; PANE_WIN_APP_OUT lays the app's folder out
# elsewhere than packaging/build/windows/Application; PANE_7ZA names 7-Zip,
# the checkout's own by default, third_party/lzma_sdk/bin/host_platform/7za)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_DIR="$ROOT_DIR/packaging"
CHROMIUM_ROOT="${CHROMIUM_ROOT:-$HOME/chromium}"
ENGINE_OUT="${PANE_WIN_ENGINE_OUT:-$CHROMIUM_ROOT/src/out/Win}"
APP="${PANE_WIN_APP_OUT:-$APP_DIR/build/windows/Application}"
SEVEN_ZIP="${PANE_7ZA:-$CHROMIUM_ROOT/src/third_party/lzma_sdk/bin/host_platform/7za}"
RELEASE_LIST="$APP_DIR/windows/pane.release"
VERSION="$(tr -d '[:space:]' < "$APP_DIR/VERSION")"
ARCHIVE="$APP_DIR/release/Pane-$VERSION-x64-app.7z"

die() {
	echo "error: $*" >&2
	exit 1
}

[ -f "$ENGINE_OUT/chrome.exe" ] && [ -f "$ENGINE_OUT/chrome.dll" ] ||
	die "no Windows engine in $ENGINE_OUT: build out/Win (docs/building.md), or name an unpacked engine archive with PANE_WIN_ENGINE_OUT"
[ -x "$SEVEN_ZIP" ] || die "no 7-Zip at $SEVEN_ZIP: sync the checkout (its DEPS fetch it), or name one with PANE_7ZA"

echo "== lay out $APP"
rm -rf "$APP"
mkdir -p "$APP"

# The engine's files, each at its path in the engine's folder
# (windows-engine-files.sh, pane.release's [engine]).
files="$("$ROOT_DIR/scripts/windows-engine-files.sh" "$ENGINE_OUT")"

while IFS= read -r relative; do
	mkdir -p "$APP/$(dirname "$relative")"
	cp -p "$ENGINE_OUT/$relative" "$APP/$relative"
done <<< "$files"

# Pane's version, which Windows' Pane reads beside chrome.dll
# (pane/app/version_win.cc): the bare version, as the Mac's PaneVersion.
printf '%s' "$VERSION" > "$APP/pane-version"

# Chrome's CRLSet, which the engine hands its certificate verifier at start
# (ADR 0040), the same file the Mac's package carries.
CRLSET="$APP_DIR/build/crl-set"

if [ -z "$(find "$CRLSET" -mtime -1 2>/dev/null)" ]; then
	mkdir -p "$(dirname "$CRLSET")"

	if "$ROOT_DIR/scripts/fetch-crlset.sh" "$CRLSET.new"; then
		mv "$CRLSET.new" "$CRLSET"
	else
		rm -f "$CRLSET.new"
		echo "warning: no new CRLSet: the package carries the last one fetched, if any" >&2
	fi
fi

# A release blocks the certificates Chrome does (ADR 0040).
[ -f "$CRLSET" ] || die "no CRLSet to package (fetch-crlset.sh)"
cp "$CRLSET" "$APP/crl-set"

# What pane.release's [package] names, this script writes.
while IFS= read -r file; do
	[ -f "$APP/$file" ] || die "pane.release's [package] names $file, which package-win.sh does not write"
done < <(awk '/^\[/ { section = $0; next } section == "[package]" && !/^#/ && NF { print $1 }' "$RELEASE_LIST")

# Flat: chrome.exe loads the chrome.dll beside it only where no version
# folder is (chrome/app/main_dll_loader_win.cc).
for folder in "$APP"/*/; do
	case "$(basename "$folder")" in
	*.*.*.*) die "$folder is a version folder, which the flat layout has none of" ;;
	esac
done

echo "$(find "$APP" -type f | wc -l | tr -d ' ') files, $(du -sh "$APP" | cut -f1)"

echo "== compress $ARCHIVE"
mkdir -p "$(dirname "$ARCHIVE")"
rm -f "$ARCHIVE"
# The folder's contents at the archive's root, so it unpacks into whichever
# folder an install or an update names.
(
	cd "$APP"
	"$SEVEN_ZIP" a -t7z \
		-m0=BCJ2 -m1=LZMA:d27:fb128 -m2=LZMA:d22:fb128:mf=bt2 -m3=LZMA:d22:fb128:mf=bt2 \
		-mb0:1 -mb0s1:2 -mb0s2:3 \
		"$ARCHIVE" ./* > /dev/null
)

shasum -a 256 "$ARCHIVE"
echo "packaged $ARCHIVE"
