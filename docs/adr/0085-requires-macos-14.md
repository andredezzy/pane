# The app requires macOS 14, as its executable does

The app's bundle declares the minimum macOS it runs on, 14.0, so Finder tells a user on macOS 13 that Pane needs 14, where it would have opened a Pane that cannot start, with no word why. The bundle's executable is Chromium's loader, and its minimum is `mac_min_system_version` in `chromium/args/release.gn`, 14.0, which gn writes into its Info.plist (`LSMinimumSystemVersion`) over Chromium's 13.0; `package.sh` writes none of its own, and compiles the icon for the same minimum.

It was found before the first native release, which every Electron Pane's update check offers whatever its macOS (ADR 0002, ADR 0051). Until 0.20.0 the minimum was the Swift executable's, which `package.sh` wrote into the bundle over Chromium's.
