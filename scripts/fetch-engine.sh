#!/usr/bin/env bash
# Fetches the prebuilt engine made from this checkout's engine sources and
# prints its directory, a stand-in for the engine's out/Release: the app then
# builds and packages without a Chromium checkout (Ardia's prebuilt-engines.tsv
# model). prebuilt-engines.tsv names the engine key (engine-key.sh) of the
# engine-<key:12> GitHub release carrying PaneCore-<chromium>-<key:12>.zip, and
# its SHA-256.
# Usage: PANE_ENGINE_OUT="$(fetch-engine.sh)" package.sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ASSETS="https://github.com/andredezzy/pane/releases/download"
# Out of Spotlight (.noindex): it would register each engine's bundle with
# LaunchServices, which opens the newest copy of Pane's identifier for a link,
# and an engine carries Chromium's version, newer than any Pane's.
STORE="$ROOT_DIR/packaging/build/engines.noindex"

die() {
	echo "error: $*" >&2
	exit 1
}

key="$("$ROOT_DIR/scripts/engine-key.sh")"
version="$(tr -d '[:space:]' < "$ROOT_DIR/chromium/CHROMIUM_VERSION")"
entry="$(awk -v key="$key" '$1 == key { print; exit }' "$ROOT_DIR/chromium/prebuilt-engines.tsv")"

[ -n "$entry" ] || die "no prebuilt engine for this checkout's engine sources ($key): build the engine (docs/building.md), then publish it as engine-${key:0:12} with scripts/publish-engine.sh (chromium/UPGRADING.md, step 6)"
read -r _ sha256 <<< "$entry"
release="engine-${key:0:12}"

name="PaneCore-$version-${key:0:12}.zip"
dest="$STORE/${key:0:12}"

if [ ! -f "$dest/.complete" ]; then
	work="$(mktemp -d)"
	trap 'rm -rf "$work"' EXIT

	echo "downloading $name from $release" >&2
	curl -fL --retry 3 --progress-bar -o "$work/$name" "$ASSETS/$release/$name" || die "could not download $ASSETS/$release/$name"
	echo "$sha256  $work/$name" | shasum -a 256 -c --quiet || die "$name does not match its SHA-256 in prebuilt-engines.tsv"

	ditto -x -k "$work/$name" "$work/engine"
	[ -f "$work/engine/Pane Framework.framework/Versions/Current/Pane Framework" ] || die "$name holds no Pane Framework.framework"
	[ -x "$work/engine/pane-volume-watch" ] || die "$name holds no pane-volume-watch"
	touch "$work/engine/.complete"

	# Only the engine in use is kept: each is ~300 MB.
	rm -rf "$STORE"
	mkdir -p "$STORE"
	mv "$work/engine" "$dest"
fi

echo "$dest"
