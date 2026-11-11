#!/usr/bin/env bash
# Builds and runs the model's tests under AddressSanitizer, with DCHECKs on:
# pane_model_unittests, pane/model's and the Mac's vault's tests alone, over
# //base and //crypto and not pane_unittests' network stack, in out/ASan
# (chromium/args/asan.gn; docs/building.md, Engine unit tests). gn generates
# for that one target (--root-target), so the build compiles //base, ICU,
# BoringSSL, and the model, never the browser. A gate before each push of an
# engine change (AGENTS.md); ThreadSanitizer doesn't build on the Mac.
#
# It runs under nice with PANE_BUILD_JOBS jobs (default 6), never beside
# another build, as a build may never take the Mac from its user
# (docs/adr/0007-chromium-checkout.md).
# Usage: test-asan.sh [test flags...]   (exits as the tests do)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CHROMIUM_ROOT="${CHROMIUM_ROOT:-$HOME/chromium}"
JOBS="${PANE_BUILD_JOBS:-6}"
OUT_DIR="out/ASan"

export PATH="$CHROMIUM_ROOT/depot_tools:$PATH"
export DEPOT_TOOLS_METRICS=0

cd "$CHROMIUM_ROOT/src"
mkdir -p "$OUT_DIR"

# Rewriting an unchanged args.gn would force gn to regenerate every ninja file.
if ! cmp -s "$ROOT_DIR/chromium/args/asan.gn" "$OUT_DIR/args.gn"; then
	cp "$ROOT_DIR/chromium/args/asan.gn" "$OUT_DIR/args.gn"
	gn gen "$OUT_DIR" --root-target=//pane/model:pane_model_unittests
fi

nice -n 10 autoninja -j "$JOBS" -C "$OUT_DIR" pane_model_unittests
nice -n 10 "$OUT_DIR/pane_model_unittests" "$@"
