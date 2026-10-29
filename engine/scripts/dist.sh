#!/usr/bin/env bash
# Packages the app (package.sh) and builds its dmg, as electron-builder built
# the Electron app's: app/release/Pane-<version>-arm64.dmg, the name
# the in-app update check looks for in a GitHub release. The dmg is laid out
# by dmgbuild, the same pinned build electron-builder runs: Pane and a link to
# /Applications on Pane's own background (app/dmg/make-background.swift),
# as Dia's installer is laid out.
# Usage: dist.sh   (PANE_SKIP_PACKAGE=1 reuses app/build/Pane.app;
# PANE_DMG_OUT puts the dmg elsewhere, one to install from, the release's
# left as it is; PANE_SIGN_IDENTITY and PANE_NOTARY_PROFILE sign and notarize
# the release)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
APP_DIR="$ROOT_DIR/app"
APP="$APP_DIR/build/Pane.app"
RELEASE="$APP_DIR/release"
VERSION="$(tr -d '[:space:]' < "$APP_DIR/VERSION")"
DMG="${PANE_DMG_OUT:-$RELEASE/Pane-$VERSION-arm64.dmg}"

# electron-builder's dmgbuild bundle (dmg-builder 26.8.1): a standalone
# Python with dmgbuild 1.6.7.
DMGBUILD_URL="https://github.com/electron-userland/electron-builder-binaries/releases/download/dmg-builder@1.2.0/dmgbuild-bundle-arm64-75c8a6c.tar.gz"
DMGBUILD_SHA256="a785f2a385c8c31996a089ef8e26361904b40c772d5ea65a36001212f1fc25e0"
DMGBUILD_DIR="$APP_DIR/build/dmgbuild-75c8a6c"

if [ "${PANE_SKIP_PACKAGE:-}" != 1 ]; then
	PANE_STRIP=1 "$ROOT_DIR/engine/scripts/package.sh"
fi

[ -d "$APP" ] || { echo "error: no $APP to put in the dmg" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

if [ ! -x "$DMGBUILD_DIR/dmgbuild" ]; then
	echo "== dmgbuild"
	curl -fsSL --retry 3 -o "$work/dmgbuild.tar.gz" "$DMGBUILD_URL"
	echo "$DMGBUILD_SHA256  $work/dmgbuild.tar.gz" | shasum -a 256 -c --quiet
	mkdir -p "$work/dmgbuild"
	tar -xzf "$work/dmgbuild.tar.gz" -C "$work/dmgbuild"
	rm -rf "$DMGBUILD_DIR"
	mv "$work/dmgbuild" "$DMGBUILD_DIR"
fi

echo "== dmg"
mkdir -p "$(dirname "$DMG")"
rm -f "$DMG"
settings="$work/settings.json"

# The window shows the background's 800 by 380 points under Finder's
# 32-point title bar (the image runs taller, for a shorter one), 400 points
# from the left and centered on a 1440-point screen height, as the Electron
# app's was. Pane and Applications sit on the chevrons' line, 210 points
# either side of the middle, at 200 points with 16-point labels; the volume,
# and so the window's title, is "Pane", as Dia's is "Dia". The title bar
# shows the volume's icon beside it, app/dmg/make-volume-icon.swift's: Pane's,
# its 16-point art lowered to the title's centre line (ADR 0094).
background="$APP_DIR/dmg/background.tiff"
volume_icon="$APP_DIR/dmg/VolumeIcon.icns"
width=800
height=$(( 380 + 32 ))

cat > "$settings" <<SETTINGS
{
  "title": "Pane",
  "icon-size": 200,
  "text-size": 16,
  "compression-level": 9,
  "format": "UDZO",
  "contents": [
    { "x": $(( width / 2 - 210 )), "y": 186, "path": "$APP" },
    { "x": $(( width / 2 + 210 )), "y": 186, "type": "link", "path": "/Applications" }
  ],
  "icon": "$volume_icon",
  "background": "$background",
  "window": {
    "position": { "x": 400, "y": $(( (1440 - height + 1) / 2 )) },
    "size": { "width": $width, "height": $height }
  }
}
SETTINGS

PYTHONIOENCODING=utf8 "$DMGBUILD_DIR/dmgbuild" -s "$settings" "Pane" "$DMG"

# With a Developer ID the dmg is signed too, and with a notarytool profile
# (xcrun notarytool store-credentials) it is notarized, which covers the app
# inside, and the ticket stapled to it: Gatekeeper then opens both without a
# warning. Without them, the app is ad-hoc signed and Gatekeeper warns on
# other Macs, as with the Electron app's dmg.
if [ "${PANE_SIGN_IDENTITY:--}" != - ]; then
	echo "== sign dmg"
	codesign --force --sign "$PANE_SIGN_IDENTITY" --timestamp "$DMG"

	if [ -n "${PANE_NOTARY_PROFILE:-}" ]; then
		echo "== notarize"
		xcrun notarytool submit "$DMG" --keychain-profile "$PANE_NOTARY_PROFILE" --wait
		xcrun stapler staple "$DMG"
		spctl --assess --type open --context context:primary-signature --verbose "$DMG"
	fi
fi

shasum -a 256 "$DMG"
echo "built $DMG"
