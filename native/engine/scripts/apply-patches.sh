#!/usr/bin/env bash
# Applies patches/series to ~/chromium/src. Re-running with an unchanged series
# is a no-op, and a run that stopped at a conflict picks up after the patches
# already applied. A changed series first reverses what was applied before,
# from the copies kept in ~/chromium/.applied, so the checkout never needs a
# reset.
#
# A patch that no longer applies (a Chromium upgrade moved its context) is
# merged three ways from the blobs it was made against. Conflicts stop the run
# with their markers in the files: resolve them, regenerate that patch
# (make-patch.sh), and run this again. native/engine/UPGRADING.md has the
# whole routine.
#
# Usage: apply-patches.sh             apply the series
#        apply-patches.sh --reverse   take every applied patch out again; a
#                                     run that stops on one resumes there
#        apply-patches.sh --check     exit 0 when what is applied is the
#                                     series, as it is now (build.sh asks)
set -euo pipefail

ENGINE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CHROMIUM_ROOT="${CHROMIUM_ROOT:-$HOME/chromium}"
PATCHES="$ENGINE_DIR/patches"
APPLIED="$CHROMIUM_ROOT/.applied"
SRC="$CHROMIUM_ROOT/src"

current="$(cat "$APPLIED/state" 2>/dev/null || true)"

# Last applied, first out. Each patch leaves the record as it comes out, so
# a run that stops on one (a hand edit in a file it patched) resumes there
# once that is sorted out, instead of failing on the patches already out.
reverse() {
	while [ -s "$APPLIED/state" ]; do
		name="$(tail -n 1 "$APPLIED/state" | cut -d' ' -f2)"

		if ! git -C "$SRC" apply -R "$APPLIED/$name"; then
			echo "cannot take $name out: its files changed since it was applied. Undo that change and run this again; the patches after it are already out" >&2
			exit 1
		fi

		sed -i '' '$d' "$APPLIED/state"
		rm -f "$APPLIED/$name"
		echo "reversed $name"
	done

	rm -rf "$APPLIED"
	current=""
}

if [ "${1:-}" = --reverse ]; then
	reverse
	exit 0
fi

wanted="$(grep -v '^\s*\(#\|$\)' "$PATCHES/series" | while read -r name; do
	printf '%s %s\n' "$(shasum -a 256 "$PATCHES/$name" | cut -d' ' -f1)" "$name"
done)"

if [ "${1:-}" = --check ]; then
	[ "$wanted" = "$current" ]
	exit
fi

if [ "$wanted" = "$current" ]; then
	echo "patches up to date"
	exit 0
fi

# What is applied is the start of the series: apply the rest. Anything else
# is taken out first.
if [ -n "$current" ] && [ "${wanted:0:${#current}}" = "$current" ] && [ "${wanted:${#current}:1}" = $'\n' ]; then
	remaining="${wanted:${#current}+1}"
else
	reverse
	remaining="$wanted"
fi

mkdir -p "$APPLIED"

while read -r hash name; do
	[ -z "$name" ] && continue

	if ! git -C "$SRC" apply --whitespace=nowarn "$PATCHES/$name" 2>/dev/null; then
		status=0
		git -C "$SRC" apply --3way --whitespace=nowarn "$PATCHES/$name" || status=$?
		conflicts="$(git -C "$SRC" diff --name-only --diff-filter=U)"
		# A three-way apply stages its files; the checkout's index stays clean.
		git -C "$SRC" reset -q

		if [ -n "$conflicts" ]; then
			echo "conflicts in $name, marked in:" >&2
			while read -r file; do
				echo "  $file"
			done <<< "$conflicts" >&2
			echo "resolve them, then: make-patch.sh $name && apply-patches.sh" >&2
			exit 1
		elif [ "$status" -ne 0 ]; then
			echo "$name does not apply, even three ways" >&2
			exit 1
		fi

		echo "merged $name"
	else
		echo "applied $name"
	fi

	cp "$PATCHES/$name" "$APPLIED/$name"
	printf '%s %s\n' "$hash" "$name" >> "$APPLIED/state"
done <<< "$remaining"
