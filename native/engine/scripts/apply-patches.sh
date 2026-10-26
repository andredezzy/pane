#!/usr/bin/env bash
# Applies patches/series to ~/chromium/src. Re-running with an unchanged series
# is a no-op; a changed series first reverses what was applied before, from the
# copies kept in ~/chromium/.applied, so the checkout never needs a reset.
set -euo pipefail

ENGINE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CHROMIUM_ROOT="${CHROMIUM_ROOT:-$HOME/chromium}"
PATCHES="$ENGINE_DIR/patches"
APPLIED="$CHROMIUM_ROOT/.applied"
SRC="$CHROMIUM_ROOT/src"

wanted="$(grep -v '^\s*\(#\|$\)' "$PATCHES/series" | while read -r name; do
	printf '%s %s\n' "$(shasum -a 256 "$PATCHES/$name" | cut -d' ' -f1)" "$name"
done)"

current="$(cat "$APPLIED/state" 2>/dev/null || true)"

if [ "$wanted" = "$current" ]; then
	echo "patches up to date"
	exit 0
fi

if [ -n "$current" ]; then
	tail -r <<< "$current" | while read -r _ name; do
		git -C "$SRC" apply -R "$APPLIED/$name"
		echo "reversed $name"
	done
fi

rm -rf "$APPLIED"
mkdir -p "$APPLIED"

while read -r _ name; do
	[ -z "$name" ] && continue
	git -C "$SRC" apply --whitespace=nowarn "$PATCHES/$name"
	cp "$PATCHES/$name" "$APPLIED/$name"
	echo "applied $name"
done <<< "$wanted"

printf '%s\n' "$wanted" > "$APPLIED/state"
