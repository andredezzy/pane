#!/usr/bin/env bash
# Brings ~/chromium/src to the tag in CHROMIUM_VERSION, with all deps and hooks,
# and links //pane to native/engine/pane. Safe to re-run: finished steps are
# skipped, so a second run on a synced checkout changes nothing.
set -euo pipefail

ENGINE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CHROMIUM_ROOT="${CHROMIUM_ROOT:-$HOME/chromium}"
VERSION="$(tr -d '[:space:]' < "$ENGINE_DIR/CHROMIUM_VERSION")"
DEPOT_TOOLS="$CHROMIUM_ROOT/depot_tools"
SRC_URL="https://chromium.googlesource.com/chromium/src.git"

export PATH="$DEPOT_TOOLS:$PATH"
export DEPOT_TOOLS_METRICS=0

mkdir -p "$CHROMIUM_ROOT"
cd "$CHROMIUM_ROOT"

if [ ! -d "$DEPOT_TOOLS/.git" ]; then
	git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS"
fi

# Unmanaged: gclient syncs DEPS but never moves src off the tag we check out.
cat > .gclient <<GCLIENT
solutions = [
  {
    "name": "src",
    "url": "$SRC_URL",
    "managed": False,
    "custom_deps": {},
    "custom_vars": {},
  },
]
target_os = ["mac"]
GCLIENT

if [ ! -d src/.git ]; then
	git clone -c advice.detachedHead=false --depth 1 --branch "$VERSION" "$SRC_URL" src
fi

current="$(git -C src describe --tags --exact-match HEAD 2>/dev/null || true)"

if [ "$current" != "$VERSION" ]; then
	# Moving to another tag under applied patches would strand them: their
	# record in .applied would no longer describe the tree.
	if [ -s "$CHROMIUM_ROOT/.applied/state" ]; then
		echo "error: Pane's patches are applied to src; take them out first (apply-patches.sh --reverse)" >&2
		exit 1
	fi

	git -C src fetch --depth 1 origin "refs/tags/$VERSION:refs/tags/$VERSION"
	git -C src -c advice.detachedHead=false checkout --force "refs/tags/$VERSION"
	rm -f "$CHROMIUM_ROOT"/.synced-* "$CHROMIUM_ROOT"/.hooks-*
fi

if [ ! -f "$CHROMIUM_ROOT/.synced-$VERSION" ]; then
	gclient sync --no-history --shallow --nohooks -D --force
	touch "$CHROMIUM_ROOT/.synced-$VERSION"
fi

if [ ! -f "$CHROMIUM_ROOT/.hooks-$VERSION" ]; then
	gclient runhooks
	touch "$CHROMIUM_ROOT/.hooks-$VERSION"
fi

if [ "$(readlink src/pane 2>/dev/null || true)" != "$ENGINE_DIR/pane" ]; then
	ln -sfn "$ENGINE_DIR/pane" src/pane
fi

echo "synced $VERSION"
