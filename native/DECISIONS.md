# Engineering decisions log

Dated record of choices made while building the native shell — what was decided, why, and what was rejected. Newest last.

## 2026-10-26

- **Pinned Chromium 156.0.8078.48**, the newest Mac stable on chromiumdash today. Ardia's patch series targets 156.0.8078.47, one patch release behind, so its patches should apply with little or no fuzz.
- **Native data root is `~/Library/Application Support/com.pane.desktop/`** (ADR 0004): it collides with none of the Electron app's directories (`@pane/veil`, `@pane/desktop`, `Pane`).
- **Checkout via a shallow git clone of the tag plus an unmanaged `.gclient`** (`--no-history --shallow`), rather than the official source tarball, which is Linux-oriented and skips Mac-only deps and hooks.
- **Patches are applied and reversed with saved copies** (`~/chromium/.applied`) instead of resetting the checkout, so `src/pane` and build outputs are never touched by a reset.
- **Chrome for Testing 156.0.8078.48** (`~/chromium/cft`) is used only to validate the scenario runner before the first engine build exists; it is never shipped or patched.
- **Dropped `enable_nacl` from the gn args**: Chromium 156 no longer declares it (NaCl is gone), and gn warned the arg had no effect.
- **The Metal toolchain is a separate Xcode 27 component**: ANGLE's Metal shaders need `xcrun metal`, which Xcode 27 does not install by default. Installed with `xcodebuild -downloadComponent MetalToolchain` (no admin rights needed); `native/README.md` lists it as a prerequisite.
- **Engine user data dir is `<data root>/Engine`, profiles are `<data root>/Engine/<profile id>`**: `ProfileManager::IsAllowedProfilePath` only accepts profile directories whose parent is the user data dir, so `<data root>/Profiles/<id>` (the plan's first sketch) would be refused. Pane's own JSON state stays at `<data root>/`.
- **Kernel panic during the first engine build (watchdog timeout, 01:41 local)**: `watchdog timeout: no checkins from watchdogd in 93 seconds`, panicking thread a `clang` from the build. The build ran ~18 compile jobs while Spotlight (`mds`) indexed the fresh 500k-file checkout; a jetsam event 40 min earlier showed memory pressure during the dependency sync. Fixes:
  - **The checkout lives in `~/chromium.noindex`** (Spotlight skips `.noindex` directories), and `~/chromium` is a symlink to it, so every path in the plan and the scripts still works. Chromium's own Mac build docs recommend keeping the checkout out of Spotlight.
  - **`build.sh` caps compile jobs** at two thirds of the cores (12 of 18 here, override with `PANE_BUILD_JOBS`) and runs them at utility QoS under `nice`, leaving the system headroom.
