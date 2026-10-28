#!/usr/bin/env bash
# Fetches the prebuilt engine made from this checkout's engine sources and
# prints its directory, a stand-in for the engine's out/Release: the app then
# builds and packages without a Chromium checkout (Ardia's prebuilt-engines.tsv
# model). prebuilt-engines.tsv names, per engine key (engine-key.sh), the
# GitHub release carrying PaneCore-<chromium>-<key>.zip and its SHA-256.
# Usage: PANE_ENGINE_OUT="$(fetch-engine.sh)" package.sh
#   PANE_PREBUILT_ASSETS overrides where releases' assets are downloaded from.
set -euo pipefail

ENGINE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ASSETS="${PANE_PREBUILT_ASSETS:-https://github.com/andredezzy/pane/releases/download}"
STORE="$(cd "$ENGINE_DIR/.." && pwd)/app/build/engines"

die() {
	echo "error: $*" >&2
	exit 1
}

key="$("$ENGINE_DIR/scripts/engine-key.sh")"
version="$(tr -d '[:space:]' < "$ENGINE_DIR/CHROMIUM_VERSION")"
entry="$(awk -v key="$key" '$1 == key { print; exit }' "$ENGINE_DIR/prebuilt-engines.tsv")"

[ -n "$entry" ] || die "no prebuilt engine for this checkout's engine sources ($key): build the engine (docs/building.md), then publish it as engine-${key:0:12} with engine/scripts/publish-engine.sh (engine/UPGRADING.md, step 6)"
read -r _ release sha256 <<< "$entry"

name="PaneCore-$version-${key:0:12}.zip"
dest="$STORE/${key:0:12}"

if [ ! -f "$dest/.complete" ]; then
	work="$(mktemp -d)"
	trap 'rm -rf "$work"' EXIT

	echo "downloading $name from $release" >&2
	curl -fL --retry 3 --progress-bar -o "$work/$name" "$ASSETS/$release/$name" || die "could not download $ASSETS/$release/$name"
	echo "$sha256  $work/$name" | shasum -a 256 -c --quiet || die "$name does not match its SHA-256 in prebuilt-engines.tsv"

	ditto -x -k "$work/$name" "$work/engine"
	[ -f "$work/engine/PaneCore.framework/Versions/Current/PaneCore" ] || die "$name holds no PaneCore.framework"
	touch "$work/engine/.complete"

	# Only the engine in use is kept: each is ~300 MB.
	rm -rf "$STORE"
	mkdir -p "$STORE"
	mv "$work/engine" "$dest"
fi

echo "$dest"
