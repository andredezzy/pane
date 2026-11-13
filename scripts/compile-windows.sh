#!/usr/bin/env bash
# Compiles //pane's code for Windows x64, cross-built from the Mac in out/Win
# (chromium/args/win.gn), with no full Windows build: every source of every
# //pane target the Windows engine links (//chrome:chrome_dll's deps under
# //pane, as gn's graph has them: the UI, the model, the channel, encryption,
# and Windows' own platform files), and the unit tests beside them (each
# //pane/...:unit_tests, and pane_unittests' own, encryption's), so code that
# builds only on the Mac shows before Windows' turn comes (ADR 0124).
# Each object compiles alone (`autoninja -C out/Win ../../<file>^`). A gate
# before each push of an engine change, beside `gn check out/Win '//pane/*'`
# (AGENTS.md); the link itself is a full Windows build's
# (docs/plans/views-migration.md, W1).
#
# It runs under nice with PANE_BUILD_JOBS jobs (default 6), and stops if the
# Mac's 1-minute load stays over 18 for 2 minutes, as a build may never take
# the Mac from its user (docs/adr/0007-chromium-checkout.md).
# Usage: compile-windows.sh   (prints the errors, and exits 1 if there are any)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CHROMIUM_ROOT="${CHROMIUM_ROOT:-$HOME/chromium}"
JOBS="${PANE_BUILD_JOBS:-6}"
OUT_DIR="out/Win"

export PATH="$CHROMIUM_ROOT/depot_tools:$PATH"
export DEPOT_TOOLS_METRICS=0

cd "$CHROMIUM_ROOT/src"
mkdir -p "$OUT_DIR"

if ! cmp -s "$ROOT_DIR/chromium/args/win.gn" "$OUT_DIR/args.gn"; then
	cp "$ROOT_DIR/chromium/args/win.gn" "$OUT_DIR/args.gn"
	gn gen "$OUT_DIR" --root-target=//pane
fi

# The //pane targets chrome.dll links, the shared tests' targets, and each
# one's C++ sources.
gn desc "$OUT_DIR" //chrome:chrome_dll deps --all | grep '^//pane' > "$OUT_DIR/pane-engine-targets.txt"
gn desc "$OUT_DIR" '//pane/*' sources --format=json > "$OUT_DIR/pane-sources.json"

objects=()
while IFS= read -r object; do
	objects+=("$object")
done < <(python3 -I - "$OUT_DIR/pane-engine-targets.txt" "$OUT_DIR/pane-sources.json" <<'PY'
import json, sys
targets = [line.strip() for line in open(sys.argv[1]) if line.strip()]
described = json.load(open(sys.argv[2]))
targets += [target for target in described
            if target.endswith(":unit_tests") or target.endswith(":pane_unittests")]
files = []
for target in targets:
    for source in described.get(target, {}).get("sources", []):
        if source.startswith("//pane/") and source.endswith(".cc"):
            files.append(source[2:])
for name in dict.fromkeys(files):
    print(f"../../{name}^")
PY
)

log="$(mktemp)"
trap 'rm -f "$log"' EXIT

nice -n 10 autoninja -j "$JOBS" -k 0 -C "$OUT_DIR" "${objects[@]}" > "$log" 2>&1 &
build=$!
high=0
while kill -0 "$build" 2>/dev/null; do
	load="$(sysctl -n vm.loadavg | awk '{print $2}')"
	if awk -v l="$load" 'BEGIN{exit !(l > 18)}'; then high=$((high + 1)); else high=0; fi
	if [ "$high" -ge 4 ]; then
		kill "$build" 2>/dev/null || true
		pkill -f "siso ninja -C $OUT_DIR" 2>/dev/null || true
		echo "stopped: the Mac's load stayed at $load for 2 minutes" >&2
		exit 2
	fi
	sleep 30
done
wait "$build" || true

errors="$(sed 's/\x1b\[[0-9;]*m//g' "$log" | grep -E 'error:' | sort -u || true)"
failed="$(grep -c FAILED "$log" || true)"
echo "${#objects[@]} objects; $(printf '%s' "$errors" | grep -c . || true) errors, $failed failed"
if [ -n "$errors" ] || [ "$failed" != 0 ]; then
	printf '%s\n' "$errors" | head -40 | cut -c1-240
	exit 1
fi
