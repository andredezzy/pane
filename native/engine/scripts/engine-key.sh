#!/usr/bin/env bash
# Prints the key of the engine's sources at HEAD: what a PaneCore build is
# made of (CHROMIUM_VERSION, args, patches, //pane), as one hash of their git
# trees. A prebuilt engine is matched to it (prebuilt-engines.tsv). Fails when
# those sources have uncommitted changes, which no prebuilt has.
set -euo pipefail

cd "$(dirname "$0")/.."
SOURCES=(CHROMIUM_VERSION args patches pane)

if [ -n "$(git status --porcelain -- "${SOURCES[@]}")" ]; then
	echo "error: the engine's sources have uncommitted changes, which no prebuilt engine has" >&2
	exit 1
fi

for source in "${SOURCES[@]}"; do
	git rev-parse "HEAD:./$source"
done | git hash-object --stdin
