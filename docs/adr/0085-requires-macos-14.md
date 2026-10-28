# The app requires macOS 14, as its executable does

The app's bundle declares the minimum macOS its Swift executable is built for, 14.0, so Finder tells a user on macOS 13 that Pane needs 14, where it would have opened a Pane that cannot start, with no word why. The bundle had kept the minimum in Chromium's Info.plist (13.0), while the Swift app is built for 14.0 (`app/project.yml`). `engine/scripts/package.sh` now writes the Swift app's minimum (`LSMinimumSystemVersion`) into the bundle.

It was found before the first native release, which every Electron Pane's update check offers whatever its macOS (ADR 0002, ADR 0051).
