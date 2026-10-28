#!/usr/bin/env bash
# Builds targets in ~/chromium/src/out/<Config> using args/<config>.gn.
# Usage: build.sh [config=release] [targets...=chrome]
#
# Runs at most PANE_BUILD_JOBS compile jobs (default: two thirds of the cores)
# under nice. Full parallelism while Spotlight indexed the checkout starved
# watchdogd and kernel-panicked this Mac once (see native/DECISIONS.md), so
# the system always keeps headroom.
#
# A build of `chrome` (the engine's bundle) records the engine key of its
# sources in out/<Config>/pane-engine.key (engine-key.sh), which package.sh
# checks before it archives the engine.
set -euo pipefail

ENGINE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CHROMIUM_ROOT="${CHROMIUM_ROOT:-$HOME/chromium}"
CONFIG="${1:-release}"
shift || true
TARGETS=("${@:-chrome}")
JOBS="${PANE_BUILD_JOBS:-$(( $(sysctl -n hw.ncpu) * 2 / 3 ))}"
OUT_DIR="out/$(tr '[:lower:]' '[:upper:]' <<< "${CONFIG:0:1}")${CONFIG:1}"
KEY_FILE="$OUT_DIR/pane-engine.key"

export PATH="$CHROMIUM_ROOT/depot_tools:$PATH"
export DEPOT_TOOLS_METRICS=0

cd "$CHROMIUM_ROOT/src"
mkdir -p "$OUT_DIR"

# The engine key of what this checkout builds, or nothing (and why, on
# stderr) when the checkout is not made of this repo's engine sources: another
# tag, other patches applied, or //pane linked to another clone. The key is
# the working tree's, so a build before the commit matches it after.
built_key() {
	local version
	version="$(tr -d '[:space:]' < "$ENGINE_DIR/CHROMIUM_VERSION")"

	if [ "$(git describe --tags --exact-match HEAD 2>/dev/null || true)" != "$version" ]; then
		echo "the checkout is not at $version (sync.sh)" >&2
	elif [ "$(readlink pane 2>/dev/null || true)" != "$ENGINE_DIR/pane" ]; then
		echo "src/pane does not link to $ENGINE_DIR/pane (sync.sh)" >&2
	elif ! CHROMIUM_ROOT="$CHROMIUM_ROOT" "$ENGINE_DIR/scripts/apply-patches.sh" --check; then
		echo "the patches applied are not patches/series (apply-patches.sh)" >&2
	else
		"$ENGINE_DIR/scripts/engine-key.sh" --worktree
	fi
}

# The key describes the bundle: only a build of it records one, and a build
# of it that fails, or that sources change under, records none.
records_key=false

case " ${TARGETS[*]} " in
*" chrome "*) records_key=true ;;
esac

if $records_key; then
	rm -f "$KEY_FILE"
	key="$(built_key)"
fi

# Rewriting an unchanged args.gn would force gn to regenerate every ninja file.
if ! cmp -s "$ENGINE_DIR/args/$CONFIG.gn" "$OUT_DIR/args.gn"; then
	cp "$ENGINE_DIR/args/$CONFIG.gn" "$OUT_DIR/args.gn"
	gn gen "$OUT_DIR"
fi

nice -n 10 autoninja -j "$JOBS" -C "$OUT_DIR" "${TARGETS[@]}"

if $records_key; then
	if [ -z "$key" ]; then
		echo "warning: this build matches no engine key, and package.sh will not archive it" >&2
	elif [ "$(built_key)" != "$key" ]; then
		echo "warning: the engine's sources changed during the build: build again before archiving it" >&2
	else
		echo "$key" > "$KEY_FILE"
		echo "engine key $key"
	fi
fi
