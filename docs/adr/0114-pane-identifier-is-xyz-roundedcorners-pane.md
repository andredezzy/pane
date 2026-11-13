# Pane's identifier is xyz.roundedcorners.pane

Pane's bundle identifier is `xyz.roundedcorners.pane`, the reverse of its domain, pane.roundedcorners.xyz: Pane is a project of the studio Rounded Corners (the user's call, 2026-10-30). It was `com.pane.desktop` until 0.14.0.

What it names:

- **The bundles**: `MAC_BUNDLE_ID` in `pane/BRANDING` (ADR 0009) names the app, and Chromium names the framework and the helpers after it (`.framework`, `.helper`, `.helper.renderer`, the alerts' `.framework.AlertNotificationService`).
- **The data root and the caches' folders** (ADR 0006): `~/Library/Application Support/xyz.roundedcorners.pane` and `~/Library/Caches/xyz.roundedcorners.pane`.
- **The preferences' domain**, and **the log's subsystem** (`log show --predicate 'subsystem == "xyz.roundedcorners.pane"'`, `pane/ui/interaction_log_mac.cc`).
- `model::kBundleIdentifier` (`pane/model/mac/bundle_identifier.h`) holds it for the data root, the caches, and the preferences, beside the former one.

What moves at the first launch after (`model::MoveFormerData`, from the launch's `PrepareData` in `pane/app/data_launch_mac.cc`, before the engine starts), before anything reads the data:

- **The data root and the caches**, a rename each on the same volume. The data root holds ADR 0113's volume, its sealed key, and everything else of the profiles. Nothing moves once the new data root exists.
- **The window's frame**, from the former preferences, whose domain then goes: it kept the open panel's last folder (ADR 0113). Under a fixed home (scenarios), the data is the run's but the preferences are the user's own, since cfprefsd ignores a fixed home: they stay.
- **The proxy passwords' Keychain item an earlier Pane kept** (ADR 0037), whose account is the data root's path, is read under the former path too.

What macOS keeps by identifier, and starts over:

- **The default browser**: the user sets Pane again (System Settings › Desktop & Dock). macOS falls back to Safari meanwhile.
- **Camera, microphone, and location grants**, and notifications' permission: pages ask again.

The update from an earlier Pane:

- **An earlier Pane installs an update only when its identifier matches its own** (its update installer, ADR 0092; `pane/updates/update_installer_mac.cc` keeps the rule), so 0.13.1 and before do not install this one: their update opens the dmg, and the user drags Pane to /Applications once, over the earlier copy. Updates install themselves again after.

The engine:

- **Only `MAC_BUNDLE_ID` changed in `BRANDING`.** It fills the bundles' Info.plist, never compiled code, so the engine built again in minutes.
- **`COMPANY_FULLNAME`, `COMPANY_SHORTNAME`, and `COPYRIGHT` said Pane until the review of 0.18.0**: `chrome_version.h` compiles them into most of Chromium, so changing them took a full build, the one that moved `BRANDING` into `//pane` (ADR 0009). They say Rounded Corners, and `chromium/UPGRADING.md` has the step for what a new tag's `BRANDING` adds.

What was rejected:

- **Keeping `com.pane.desktop`**: an identifier names its maker by the maker's domain, as `com.google.Chrome` does.
- **`com.roundedcorners.pane`**: that is the reverse of roundedcorners.com, a domain the studio does not hold.
- **Setting the identifier in `package.sh`, over the engine's bundle**: the framework and the helpers would keep the former one, which Chromium names after the app's.
- **Asking macOS to make Pane the default browser again at the first launch**: a system dialog at launch that no board draws (ADR 0058); the user sets it as for any browser.

How it is checked:

- **`BundleIdentifierTest.MoveFormerData`** in `pane_unittests`, PaneKit's ported, with identifiers of its own (a persistent domain is the whole Mac's): the data root and the caches move, the window's frame comes along, the former domain goes, and a former folder that turns up later moves nowhere.
- **`identifier-move`**: Pane derives its data root from a throwaway home of the scenario's (`derivedDataRoot`, `tests/lib/pane.ts`; `tests/lib/browser.ts` refuses a home not fixed for Foundation too), finds an earlier Pane's data under the former identifier, shows its profiles, seals `profiles.json`, and leaves no former folder; the user's own folders are checked untouched.
- **`update-install`** installs an update of the same identifier, the new one; **`interaction-timing`** and the rigs' timings read the log under the new subsystem (`tests/lib/signposts.ts`).
- **Every bundle of the packaged app** names the new identifier, and none the former: the app, the framework, and its eight helpers (`plutil -extract CFBundleIdentifier raw`).
- **The upgrade from 0.13.1, once** (2026-10-30), on a throwaway home: the installed 0.13.1 signed in to a page, its profile in the clear under `com.pane.desktop`; this build moved the data root, moved the profile into the encrypted volume behind its link, and the page was still signed in, its cookie and local storage kept.
