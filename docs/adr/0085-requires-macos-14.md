# The app requires macOS 14, as its executable does

> **On the Views line** (`docs/plans/views-migration.md`, G1 and K3): the bundle's executable is Chromium's loader, and its minimum is `mac_min_system_version` in `chromium/args/release.gn`, 14.0, which gn writes into its Info.plist, over Chromium's 13.0; `app/project.yml`, which set the Swift executable's, is gone, and `package.sh` writes no minimum of its own.

The app's bundle declares the minimum macOS its Swift executable is built for, 14.0, so Finder tells a user on macOS 13 that Pane needs 14, where it would have opened a Pane that cannot start, with no word why. The bundle had kept the minimum in Chromium's Info.plist (13.0), while the Swift app is built for 14.0 (`app/project.yml`). `scripts/package.sh` now writes the Swift app's minimum (`LSMinimumSystemVersion`) into the bundle.

It was found before the first native release, which every Electron Pane's update check offers whatever its macOS (ADR 0002, ADR 0051).
