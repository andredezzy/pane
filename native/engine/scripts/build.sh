#!/usr/bin/env bash
# Builds targets in ~/chromium/src/out/<Config> using args/<config>.gn.
# Usage: build.sh [config=release] [targets...=chrome]
#
# Runs at most PANE_BUILD_JOBS compile jobs (default: two thirds of the cores)
# at utility QoS. Full parallelism starved watchdogd and kernel-panicked this
# Mac once (see native/DECISIONS.md), so the system always keeps headroom.
set -euo pipefail

ENGINE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CHROMIUM_ROOT="${CHROMIUM_ROOT:-$HOME/chromium}"
CONFIG="${1:-release}"
shift || true
TARGETS=("${@:-chrome}")
JOBS="${PANE_BUILD_JOBS:-$(( $(sysctl -n hw.ncpu) * 2 / 3 ))}"
OUT_DIR="out/$(tr '[:lower:]' '[:upper:]' <<< "${CONFIG:0:1}")${CONFIG:1}"

export PATH="$CHROMIUM_ROOT/depot_tools:$PATH"
export DEPOT_TOOLS_METRICS=0

cd "$CHROMIUM_ROOT/src"
mkdir -p "$OUT_DIR"

# Rewriting an unchanged args.gn would force gn to regenerate every ninja file.
if ! cmp -s "$ENGINE_DIR/args/$CONFIG.gn" "$OUT_DIR/args.gn"; then
	cp "$ENGINE_DIR/args/$CONFIG.gn" "$OUT_DIR/args.gn"
	gn gen "$OUT_DIR"
fi

taskpolicy -c utility nice -n 10 autoninja -j "$JOBS" -C "$OUT_DIR" "${TARGETS[@]}"
