#!/usr/bin/env bash
# Checks that //pane tests for the OS only where a platform's own code goes:
# its BUILD.gn files, and the platform files that implement one header each,
# named <thing>_mac.mm (or _mac.cc, _mac.h) and <thing>_win.cc (or _win.h)
# beside <thing>.h (ADR 0124, One code path). A shared
# file that asks which OS it runs on is how a second UI starts: one per OS.
# Lists each test of the OS outside them, in the files git tracks, and exits
# 1 if there is one. Needs git alone, so CI runs it too
# (.github/workflows/gates.yml).
# Usage: check-os-flags.sh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# The build flags, and the compilers' own macros for the same tests.
PATTERN='BUILDFLAG\(IS_(MAC|WIN|APPLE|IOS)\)|defined\(OS_(MAC|MACOSX|WIN|APPLE)\)|__APPLE__|__MACH__|\b_WIN32\b|\b_WIN64\b|TARGET_OS_'

found="$(git -C "$ROOT_DIR" grep -n -E "$PATTERN" -- \
	'pane/*.cc' 'pane/*.h' 'pane/*.mm' 'pane/*.c' \
	':!*_mac.mm' ':!*_mac.cc' ':!*_mac.h' ':!*_win.cc' ':!*_win.h' || true)"

if [ -n "$found" ]; then
	echo "$found"
	echo "error: a shared file of //pane tests for the OS: move what differs into <thing>_mac.mm and <thing>_win.cc beside <thing>.h, or into BUILD.gn" >&2
	exit 1
fi

echo "no OS flag outside //pane's platform files"
