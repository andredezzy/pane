# Pane's icon, dated so macOS cannot show Chromium's

The bundle carries Pane's icon in place of Chromium's, and every app inside it takes the build's date, because macOS caches an app's icon by its path and date and kept showing Chromium's. The app has its own asset catalog (`app/Pane/Assets.xcassets`): `AppIcon`, from the Electron app's icon (`apps/veil/resources/icon.png` on the `electron` branch), and `Icon`, the badge Chromium's document types name. `engine/scripts/package.sh` copies the Swift build's `Assets.car` and `AppIcon.icns` over the engine bundle's, which were Chromium's, removes Chromium's `app.icns`, and points `CFBundleIconFile` at `AppIcon`.

Finder then showed Pane's icon, but the Dock kept Chromium's. `package.sh` copies the engine's bundle with `ditto`, which keeps its dates, so `Pane.app` kept the date it had when its icon was Chromium's (2026-10-26 04:12). LaunchServices had the right `AppIcon`; the Dock kept its cached image. `package.sh` now gives every `.app` in the bundle the build's date and registers the app with LaunchServices (`lsregister -f`).

The notification helpers (`Pane Helper (Alerts)`), whose icon notifications show, still carried Chromium's `app.icns` and `Assets.car`. They take Pane's too.
