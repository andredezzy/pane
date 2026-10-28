#!/usr/bin/env bash
# Lists every exported Pane symbol of the built PaneCore in
# chrome/app/framework.order, in the linker's order. Chrome's build checks
# that the framework exports nothing the order file doesn't name, so run this
# after adding a class or constant to the public API, then rebuild and
# regenerate patch 0001 (make-patch.sh 0001-pane-framework.patch).
set -euo pipefail

CHROMIUM_ROOT="${CHROMIUM_ROOT:-$HOME/chromium}"
SRC="$CHROMIUM_ROOT/src"
VERSION="$(tr -d '[:space:]' < "$(cd "$(dirname "$0")/.." && pwd)/CHROMIUM_VERSION")"
BINARY="$SRC/out/Release/PaneCore.framework/Versions/$VERSION/PaneCore"
ORDER="$SRC/chrome/app/framework.order"
MARKER="# Pane: the embedding API the Pane app links against."

python3 - "$BINARY" "$ORDER" "$MARKER" <<'PY'
import subprocess, sys
binary, order, marker = sys.argv[1:]
exports = subprocess.check_output(["nm", "-jUng", binary], text=True).split()
symbols = [s for s in exports if "Pane" in s]
text = open(order).read()
head = text[:text.index(marker)] if marker in text else text.rstrip("\n") + "\n\n"
open(order, "w").write(head + marker + "\n"
    "# Every symbol framework.exports lets through must be listed here, in the\n"
    "# order the linker lays them out (native/engine/scripts/update-exports.sh).\n"
    + "\n".join(symbols) + "\n")
print(f"{len(symbols)} Pane exports listed in {order}")
PY
