#!/usr/bin/env bash
# Compiles //pane's shared code for Windows x64, cross-built from the Mac in
# out/Win (chromium/args/win.gn), with no full Windows build: the sources
# pane/BUILD.gn builds on every OS, and those of the targets beside it that do
# (pane/model, pane/ui/app, pane/ui/design, pane/ui/lock, pane/ui/tabs). Each
# object compiles alone (`autoninja -C out/Win ../../<file>^`), so code that
# builds only on the Mac shows before Windows' turn comes (ADR 0124). A gate
# before each push of an engine change, beside `gn check out/Win '//pane/*'`
# (AGENTS.md).
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

objects=()
while IFS= read -r object; do
	objects+=("$object")
done < <(python3 -I - "$ROOT_DIR" <<'PY'
import pathlib, re, sys
root = pathlib.Path(sys.argv[1])
build = (root / "pane/BUILD.gn").read_text()
start = build.index("  sources = [")
shared = re.findall(r'"([^"]+\.cc)"', build[start:build.index("  ]", start)])
files = ["pane/" + name for name in shared]
for folder in ("pane/model", "pane/ui/app", "pane/ui/design", "pane/ui/lock", "pane/ui/tabs"):
    files += [str(path.relative_to(root)) for path in sorted((root / folder).glob("*.cc"))
              if not path.name.endswith("_mac.cc")]
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
