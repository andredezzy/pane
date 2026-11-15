#!/usr/bin/env bash
# Prints the engine's files Pane's app folder holds on Windows, as
# packaging/windows/pane.release's [engine] section names them: each one's
# path in <engine dir> (out/Win, or an unpacked engine archive), one a line,
# a glob's every match. Fails on a line that names no file there.
# package-win.sh lays the folder out from them, and publish-engine.sh
# archives them, so a fetched engine is what package-win.sh reads.
# Usage: windows-engine-files.sh <engine dir>
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="${1:?usage: windows-engine-files.sh <engine dir>}"
RELEASE_LIST="$ROOT_DIR/packaging/windows/pane.release"
section=""

while IFS= read -r line || [ -n "$line" ]; do
	line="${line%%#*}"
	line="$(printf '%s' "$line" | tr -d '[:space:]')"
	[ -n "$line" ] || continue

	case "$line" in
	\[*\])
		section="$line"
		continue
		;;
	esac

	[ "$section" = "[engine]" ] || continue
	found=0

	for file in "$ENGINE"/$line; do
		[ -f "$file" ] || continue
		printf '%s\n' "${file#"$ENGINE"/}"
		found=$((found + 1))
	done

	if [ "$found" -eq 0 ]; then
		echo "error: $ENGINE has no file for pane.release's $line" >&2
		exit 1
	fi
done < "$RELEASE_LIST"
