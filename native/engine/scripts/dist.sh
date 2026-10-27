#!/usr/bin/env bash
# Packages the app (package.sh) and builds its dmg, as electron-builder built
# the Electron app's: native/app/release/Pane-<version>-arm64.dmg, the name
# the in-app update check looks for in a GitHub release. The dmg is laid out
# by dmgbuild, the same pinned build electron-builder runs, with the same
# settings: the app, a link to /Applications, and "Install Pane.command" on
# electron-builder's arrow background.
# Usage: dist.sh   (PANE_SKIP_PACKAGE=1 reuses native/app/build/Pane.app)
set -euo pipefail

NATIVE_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
APP_DIR="$NATIVE_DIR/app"
APP="$APP_DIR/build/Pane.app"
RELEASE="$APP_DIR/release"
VERSION="$(tr -d '[:space:]' < "$APP_DIR/VERSION")"
DMG="$RELEASE/Pane-$VERSION-arm64.dmg"

# electron-builder's dmgbuild bundle (dmg-builder 26.8.1): a standalone
# Python with dmgbuild 1.6.7.
DMGBUILD_URL="https://github.com/electron-userland/electron-builder-binaries/releases/download/dmg-builder@1.2.0/dmgbuild-bundle-arm64-75c8a6c.tar.gz"
DMGBUILD_SHA256="a785f2a385c8c31996a089ef8e26361904b40c772d5ea65a36001212f1fc25e0"
DMGBUILD_DIR="$APP_DIR/build/dmgbuild-75c8a6c"

if [ "${PANE_SKIP_PACKAGE:-}" != 1 ]; then
	PANE_STRIP=1 "$NATIVE_DIR/engine/scripts/package.sh"
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
mkdir -p "$RELEASE"
rm -f "$DMG"
settings="$work/settings.json"

# electron-builder's settings for apps/veil's dmg config: the window is the
# background's size, 400 points from the left and centered on a 1440-point
# screen height.
background="$APP_DIR/dmg/background.tiff"
read -r width height < <(sips -g pixelWidth -g pixelHeight "$background" | awk '/pixelWidth/ { w = $2 } /pixelHeight/ { h = $2 } END { print w, h }')

cat > "$settings" <<SETTINGS
{
  "title": "Pane $VERSION-arm64",
  "text-size": 12,
  "compression-level": 9,
  "format": "UDZO",
  "contents": [
    { "x": 130, "y": 150, "path": "$APP" },
    { "x": 410, "y": 150, "type": "link", "path": "/Applications" },
    { "x": 270, "y": 300, "type": "file", "path": "$APP_DIR/dmg/Install Pane.command" }
  ],
  "icon": "$APP/Contents/Resources/AppIcon.icns",
  "background": "$background",
  "window": {
    "position": { "x": 400, "y": $(( (1440 - height + 1) / 2 )) },
    "size": { "width": $width, "height": $height }
  }
}
SETTINGS

PYTHONIOENCODING=utf8 "$DMGBUILD_DIR/dmgbuild" -s "$settings" "Pane $VERSION-arm64" "$DMG"

shasum -a 256 "$DMG"
echo "built $DMG"
