#!/usr/bin/env bash
# Regenerates one patch from the working tree. MANIFEST maps each touched
# Chromium file to exactly one patch, so a patch is the diff of its own files.
# Usage: make-patch.sh <patch-file-name>
set -euo pipefail

ENGINE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CHROMIUM_ROOT="${CHROMIUM_ROOT:-$HOME/chromium}"
NAME="$1"
VERSION="$(tr -d '[:space:]' < "$ENGINE_DIR/CHROMIUM_VERSION")"

files=()
while IFS=$'\t' read -r path patch; do
	[ "$patch" = "$NAME" ] && files+=("$path")
done < <(grep -v '^\s*\(#\|$\)' "$ENGINE_DIR/patches/MANIFEST")

if [ "${#files[@]}" -eq 0 ]; then
	echo "no MANIFEST entries for $NAME" >&2
	exit 1
fi

git -C "$CHROMIUM_ROOT/src" add -N -- "${files[@]}"
git -C "$CHROMIUM_ROOT/src" diff "refs/tags/$VERSION" -- "${files[@]}" > "$ENGINE_DIR/patches/$NAME"
git -C "$CHROMIUM_ROOT/src" reset -q -- "${files[@]}"
echo "wrote patches/$NAME (${#files[@]} files)"

# The tree now contains this patch, so the applied copy and its hash must say
# so (a new patch made in-tree included), or the next apply-patches.sh would
# reverse or re-apply the wrong thing. The state lists applied patches in
# series order, as apply-patches.sh writes it.
APPLIED="$CHROMIUM_ROOT/.applied"
mkdir -p "$APPLIED"
cp "$ENGINE_DIR/patches/$NAME" "$APPLIED/$NAME"
grep -v '^\s*\(#\|$\)' "$ENGINE_DIR/patches/series" | while read -r name; do
	if [ -f "$APPLIED/$name" ]; then
		printf '%s %s\n' "$(shasum -a 256 "$APPLIED/$name" | cut -d' ' -f1)" "$name"
	fi
done > "$APPLIED/state.new"
mv "$APPLIED/state.new" "$APPLIED/state"
