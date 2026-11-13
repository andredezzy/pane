#!/usr/bin/env bash
# Brings ~/chromium/src to the tag in CHROMIUM_VERSION, with all deps and hooks,
# for the Mac and Windows, and links //pane to pane. Safe to re-run:
# finished steps are skipped, so a second run on a synced checkout changes
# nothing, and a change to .gclient syncs again.
#
# Windows' toolchain is Microsoft's files, packaged from a Visual Studio
# install (docs/building.md, Chromium checkout) and kept outside the repo:
# PANE_WIN_TOOLCHAIN_DIR names the folder that holds its <hash>.zip. Without
# it, the Mac's build syncs as before and the toolchain's hook does nothing.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CHROMIUM_ROOT="${CHROMIUM_ROOT:-$HOME/chromium}"
VERSION="$(tr -d '[:space:]' < "$ROOT_DIR/chromium/CHROMIUM_VERSION")"
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
# The PGO profiles are what release.gn's chrome_pgo_phase reads (ADR 0007).
gclient_file="$(cat <<GCLIENT
solutions = [
  {
    "name": "src",
    "url": "$SRC_URL",
    "managed": False,
    "custom_deps": {},
    "custom_vars": {
      "checkout_pgo_profiles": True,
    },
  },
]
target_os = ["mac", "win"]
GCLIENT
)"

if [ "$(cat .gclient 2>/dev/null || true)" != "$gclient_file" ]; then
	printf '%s\n' "$gclient_file" > .gclient
	rm -f "$CHROMIUM_ROOT"/.synced-* "$CHROMIUM_ROOT"/.hooks-*
fi

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

if [ -n "${PANE_WIN_TOOLCHAIN_DIR:-}" ]; then
	zips=("$PANE_WIN_TOOLCHAIN_DIR"/*.zip)
	if [ "${#zips[@]}" -ne 1 ] || [ ! -f "${zips[0]}" ]; then
		echo "error: $PANE_WIN_TOOLCHAIN_DIR should hold one toolchain zip" >&2
		exit 1
	fi
	export DEPOT_TOOLS_WIN_TOOLCHAIN_BASE_URL="$PANE_WIN_TOOLCHAIN_DIR"
	# The zip is named by its own contents' hash, which stands in for the
	# one Chromium asks for.
	wanted="$(sed -n "s/^TOOLCHAIN_HASH = '\\(.*\\)'$/\\1/p" src/build/vs_toolchain.py)"
	export "GYP_MSVS_HASH_$wanted=$(basename "${zips[0]}" .zip)"
else
	export DEPOT_TOOLS_WIN_TOOLCHAIN=0
fi

if [ ! -f "$CHROMIUM_ROOT/.synced-$VERSION" ]; then
	gclient sync --no-history --shallow --nohooks -D --force
	touch "$CHROMIUM_ROOT/.synced-$VERSION"
fi

if [ ! -f "$CHROMIUM_ROOT/.hooks-$VERSION" ]; then
	gclient runhooks
	touch "$CHROMIUM_ROOT/.hooks-$VERSION"
fi

if [ "$(readlink src/pane 2>/dev/null || true)" != "$ROOT_DIR/pane" ]; then
	ln -sfn "$ROOT_DIR/pane" src/pane
fi

echo "synced $VERSION"
