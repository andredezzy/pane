#!/usr/bin/env bash
# Prints the key of the engine's sources at HEAD: what a PaneCore build is
# made of (CHROMIUM_VERSION, args, patches, //pane), as one hash of their git
# trees. A prebuilt engine is matched to it (prebuilt-engines.tsv). Fails when
# those sources have uncommitted changes, which no prebuilt has.
#
# Usage: engine-key.sh              the key at HEAD
#        engine-key.sh --worktree   the key the sources in the working tree
#                                   will have once committed as they are
#                                   (build.sh records it with each build)
set -euo pipefail

cd "$(dirname "$0")/.."
SOURCES=(CHROMIUM_VERSION args patches pane)

if [ "${1:-}" = --worktree ]; then
	# The trees `git add --all` then a commit would make, written through an
	# index of their own so the checkout's is left alone: HEAD's files, with
	# the working tree's changes and new files (ignored ones stay out).
	work="$(mktemp -d)"
	trap 'rm -rf "$work"' EXIT
	export GIT_INDEX_FILE="$work/index"
	git read-tree HEAD
	git add --all -- "${SOURCES[@]}"
	tree="$(git write-tree --prefix="$(git rev-parse --show-prefix)")"

	for source in "${SOURCES[@]}"; do
		git rev-parse "$tree:$source"
	done | git hash-object --stdin

	exit 0
fi

if [ -n "$(git status --porcelain -- "${SOURCES[@]}")" ]; then
	echo "error: the engine's sources have uncommitted changes, which no prebuilt engine has" >&2
	exit 1
fi

for source in "${SOURCES[@]}"; do
	git rev-parse "HEAD:./$source"
done | git hash-object --stdin
