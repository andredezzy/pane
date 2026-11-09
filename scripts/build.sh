#!/usr/bin/env bash
# Builds targets in ~/chromium/src/out/Release using args/release.gn.
# Usage: build.sh [targets...=chrome]
#
# Runs at most PANE_BUILD_JOBS compile jobs (default: a third of the cores)
# under nice. Full parallelism while Spotlight indexed the checkout starved
# watchdogd and kernel-panicked this Mac, and two thirds of the cores did again
# while it was in use (docs/adr/0007-chromium-checkout.md), so the system keeps
# room for its user. An idle Mac can take more: PANE_BUILD_JOBS=12.
#
# A build of `chrome` (the engine's bundle) records the engine key of its
# sources in out/Release/pane-engine.key (engine-key.sh), which
# publish-engine.sh checks before it archives the engine, and builds the
# volume's watchdog, pane-volume-watch, which the bundle carries beside its
# loader (package.sh).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CHROMIUM_ROOT="${CHROMIUM_ROOT:-$HOME/chromium}"
TARGETS=("${@:-chrome}")
JOBS="${PANE_BUILD_JOBS:-$(( $(sysctl -n hw.ncpu) / 3 ))}"
OUT_DIR="out/Release"
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
	version="$(tr -d '[:space:]' < "$ROOT_DIR/chromium/CHROMIUM_VERSION")"

	if [ "$(git describe --tags --exact-match HEAD 2>/dev/null || true)" != "$version" ]; then
		echo "the checkout is not at $version (sync.sh)" >&2
	elif [ "$(readlink pane 2>/dev/null || true)" != "$ROOT_DIR/pane" ]; then
		echo "src/pane does not link to $ROOT_DIR/pane (sync.sh)" >&2
	elif ! CHROMIUM_ROOT="$CHROMIUM_ROOT" "$ROOT_DIR/scripts/apply-patches.sh" --check; then
		echo "the patches applied are not patches/series (apply-patches.sh)" >&2
	else
		"$ROOT_DIR/scripts/engine-key.sh" --worktree
	fi
}

# The key describes the bundle: only a build of it records one, and a build
# of it that fails, or that sources change under, records none.
records_key=false

case " ${TARGETS[*]} " in
*" chrome "*)
	records_key=true
	TARGETS+=(pane-volume-watch)
	;;
esac

if $records_key; then
	rm -f "$KEY_FILE"
	key="$(built_key)"
fi

# Rewriting an unchanged args.gn would force gn to regenerate every ninja file.
if ! cmp -s "$ROOT_DIR/chromium/args/release.gn" "$OUT_DIR/args.gn"; then
	cp "$ROOT_DIR/chromium/args/release.gn" "$OUT_DIR/args.gn"
	gn gen "$OUT_DIR"
fi

nice -n 10 autoninja -j "$JOBS" -C "$OUT_DIR" "${TARGETS[@]}"

if $records_key; then
	if [ -z "$key" ]; then
		echo "warning: this build matches no engine key, and publish-engine.sh will not archive it" >&2
	elif [ "$(built_key)" != "$key" ]; then
		echo "warning: the engine's sources changed during the build: build again before archiving it" >&2
	else
		echo "$key" > "$KEY_FILE"
		echo "engine key $key"
	fi
fi
