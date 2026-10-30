#!/usr/bin/env bash
# Tells, in under a minute and with no Chromium checkout, whether
# patches/series still applies to a Chromium tag: fetches each file
# patches/MANIFEST names at that tag from Chromium's gitiles, and applies
# every patch to them, in order. A move to a new tag (engine/UPGRADING.md)
# then starts knowing which patches need work, before a sync and an engine
# build of hours.
# Usage: preflight.sh <tag>   (exit 0 when every patch applies, 1 when one
# does not, each named with git apply's reasons, 2 when gitiles did not
# answer)
set -euo pipefail

ENGINE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TAG="${1:?usage: preflight.sh <tag>}"
GITILES="https://chromium.googlesource.com/chromium/src/+/refs/tags/$TAG"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# One file at the tag, four at a time. Gitiles limits its rate (429), and
# curl waits as long as it says before trying again. A file the tag lacks
# (404) is one a patch adds, or one Chromium removed: the patch's own apply
# tells which.
fetch() {
	local path="$1" status
	mkdir -p "$work/$(dirname "$path")"
	status="$(curl -sS --retry 8 --retry-max-time 300 -o "$work/$path.b64" -w '%{http_code}' "$GITILES/$path?format=TEXT")"

	case "$status" in
	200) base64 -d < "$work/$path.b64" > "$work/$path" ;;
	404) ;;
	*) echo "error: gitiles answered $status for $path at $TAG" >&2; return 1 ;;
	esac

	rm -f "$work/$path.b64"
}
export -f fetch
export work GITILES TAG

if ! grep -v '^\s*\(#\|$\)' "$ENGINE_DIR/patches/MANIFEST" | cut -f1 |
	xargs -P 4 -I {} bash -c 'fetch "$1"' _ {}; then
	exit 2
fi

failed=0

while read -r name; do
	if output="$(git -C "$work" apply --whitespace=nowarn "$ENGINE_DIR/patches/$name" 2>&1)"; then
		echo "applies: $name"
	else
		echo "does not apply: $name"
		sed 's/^/  /' <<< "$output"
		failed=1
	fi
done < <(grep -v '^\s*\(#\|$\)' "$ENGINE_DIR/patches/series")

exit "$failed"
