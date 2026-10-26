# Engineering decisions log

Dated record of choices made while building the native shell — what was decided, why, and what was rejected. Newest last.

## 2026-10-26

- **Pinned Chromium 156.0.8078.48**, the newest Mac stable on chromiumdash today. Ardia's patch series targets 156.0.8078.47, one patch release behind, so its patches should apply with little or no fuzz.
- **Native data root is `~/Library/Application Support/com.pane.desktop/`** (ADR 0004): it collides with none of the Electron app's directories (`@pane/veil`, `@pane/desktop`, `Pane`).
- **Checkout via a shallow git clone of the tag plus an unmanaged `.gclient`** (`--no-history --shallow`), rather than the official source tarball, which is Linux-oriented and skips Mac-only deps and hooks.
- **Patches are applied and reversed with saved copies** (`~/chromium/.applied`) instead of resetting the checkout, so `src/pane` and build outputs are never touched by a reset.
- **Chrome for Testing 156.0.8078.48** (`~/chromium/cft`) is used only to validate the scenario runner before the first engine build exists; it is never shipped or patched.
