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

# The applied copy must match, or the next apply-patches.sh would reverse a
# patch that is no longer what the tree contains.
if [ -f "$CHROMIUM_ROOT/.applied/$NAME" ]; then
	cp "$ENGINE_DIR/patches/$NAME" "$CHROMIUM_ROOT/.applied/$NAME"
	sum="$(shasum -a 256 "$ENGINE_DIR/patches/$NAME" | cut -d' ' -f1)"
	sed -i '' "s/^[0-9a-f]* $NAME\$/$sum $NAME/" "$CHROMIUM_ROOT/.applied/state"
fi
