# Human checklist

Items the autonomous run could not do or verify on its own: each says why it needs a person, the exact steps, and the evidence file the result should produce. Open items first; done items move to the end with the date.

## Open

- **Phase 2 · Pointer gestures on screen — run when you are away from the Mac.** Dragging profiles and tabs in the sidebar, the profile and tab context menus, the delete-profile confirmation, the profile sheet's color and group pickers, ⌃⌘F full screen, and the Edit menu (it uses the clipboard) need the real pointer (CUA moves the mouse for drags), which would fight you while you work. The rendered rows and the page in its card are now proven on screen (`native/evidence/phase-2/unlocked/`). Nothing to do but leave the Mac unlocked and idle; the agent runs them then.

- **Phase 3 · Mail — look for a stray draft.** A scenario opened Mail at 07:08 on 2026-10-26 with an unsent compose window to `pane-scenario@example.invalid` (a `mailto:` link, which Chrome opens without asking); Mail was quit two minutes later and nothing was sent. If Mail autosaved that window, an empty draft to that address may be in Drafts: delete it. Details in `native/DECISIONS.md` (Phase 3, Incident).

- **Phase 3 · Pick a file through the open panel, by hand.** macOS 27 runs open and save panels out of process, and the scenarios answer them beside the panel. Once: open a page with `<input type=file>` in Pane (any upload form), choose a file in the panel, and check the page shows its name.

- **Phase 4 · Sign in to 1Password and NordPass, and autofill a login form.** Both install from the Web Store and their toolbar buttons open their own pages (`native/evidence/phase-4/1password.png`, `nordpass.png`; until you sign in, each opens its welcome page in a tab, as in Chrome). Signing in uses your accounts, so it is yours: in a Pane profile, sign in from each manager's button, then open `native/engine/tests/pages/password.html` (served by any scenario run, or open the file) and check each one offers to fill the form.

- **Phase 5 · Sign in to a real Google account.** Signing in uses your account, so it is yours. In a Pane profile, open `https://accounts.google.com`, sign in, and check Gmail opens signed in. Google accepted the engine in every run so far (`native/evidence/phase-5/google.json`). If it ever shows "This browser or app may not be secure", note it here: Pane no longer has a workaround for it.

- **Phase 5 · Migrate your Electron Pane, and allow its Keychain item.** The scenarios migrate a copy made by Electron itself, with Chromium's mock keychain (`native/evidence/phase-5/migration.json`). Your real cookies are encrypted with the key in the Electron app's Keychain item, which only you can let the native app read. Quit the Electron Pane, then start the native app once, with no native data yet (`~/Library/Application Support/com.pane.desktop` absent). It reads a copy of `~/Library/Application Support/@pane/veil`, never the folder itself. When macOS asks whether Pane may use "@pane/veil Safe Storage" (the Electron app's cookie key, which Pane copies into its own "Pane Safe Storage"), choose Allow. If macOS then asks about "Pane Safe Storage", choose Always Allow. Then check that your profiles, tabs, and PIN are there and that a site you were signed in to still is. With an ad-hoc signed build, macOS may ask again after each rebuild (6.2).

- **Phase 6 · 6.2 Sign and notarize with a Developer ID.** Signing needs an Apple Developer Program membership and its certificate, which are yours; until then every build is ad-hoc signed, and Gatekeeper warns on other Macs as it did for the Electron dmg. The steps are built and wait for an identity (`native/evidence/phase-6/signing-dry-run.txt` shows every call they make):
  1. In Xcode › Settings › Accounts, add your Apple ID, then Manage Certificates › + › Developer ID Application. `security find-identity -v -p codesigning` then lists "Developer ID Application: <name> (<team id>)".
  2. Store notarization credentials once, under a profile name: `xcrun notarytool store-credentials pane-notary --apple-id <apple id> --team-id <team id>` (it asks for an app-specific password from account.apple.com).
  3. Build the release: `PANE_SIGN_IDENTITY="Developer ID Application: <name> (<team id>)" PANE_NOTARY_PROFILE=pane-notary native/engine/scripts/dist.sh 2>&1 | tee native/evidence/phase-6/notarized.log`. Each part is signed with the hardened runtime as Chrome signs it, and the dmg is signed, notarized, stapled, and assessed.
  4. Check: `spctl --assess --type open --context context:primary-signature -v native/app/release/Pane-<version>-arm64.dmg` says "accepted, source=Notarized Developer ID"; install the app from the dmg and `spctl -a -vv /Applications/Pane.app` says the same; run the scenario suite on the signed app (`bun native/engine/tests/run.ts --binary <app>/Contents/MacOS/Pane --out native/evidence/phase-6/signed`) to catch an entitlement the hardened runtime needs. Append both outputs to `notarized.log`.
  5. Then 6.6: Sparkle can replace the manual dmg update flow.
  6. Passkeys: with the Developer ID, request Apple's `com.apple.developer.web-browser.public-key-credential` entitlement and add it, with a keychain access group, to `native/app/signing/app.entitlements` and a provisioning profile. Until then Chrome's WebAuthn dialog offers only security keys (`native/DECISIONS.md`, After the cutover).

- **Phase 6 · 6.5 The first CI run.** The `Native` workflow (`.github/workflows/native.yml`) runs PaneKit's tests and builds the app on the prebuilt engine on GitHub's `xcode-27` runner (macOS 27 and Xcode 27, a preview image). It needs a push and a published engine, which are yours. Its commands passed on this Mac (`native/evidence/phase-6/ci-local.txt`).
  1. Publish the engine that `native/engine/prebuilt-engines.tsv`'s last line names, as a prerelease: `gh release create engine-<key:12> native/app/release/PaneCore-<chromium>-<key:12>.zip --prerelease --title "Engine <chromium> (<key:12>)" --notes "PaneCore for native/engine/prebuilt-engines.tsv"`. Upload the file whose SHA-256 the line holds (`native/app/release/*.zip.sha256`). If you rebuilt it since, run `PANE_ENGINE_ARCHIVE=1 native/engine/scripts/dist.sh` and put the line it prints in the TSV.
  2. Push the branch: `git push origin native-shell`.
  3. Watch the run: `gh run watch $(gh run list --workflow Native --limit 1 --json databaseId --jq '.[0].databaseId')`. Save its URL and result to `native/evidence/phase-6/ci-first-run.txt`.
  4. If GitHub renames the `xcode-27` label (when the image leaves preview), update `runs-on` and `.github/actionlint.yaml`.

- **Phase 3 · Picture-in-picture uses Chromium's player — review.** The floating video player is Chromium's own window (the only surface that can show the video's compositor frame; AVKit's picture-in-picture plays AVFoundation media only). Nothing to do unless it should look different; the reasoning is in `native/DECISIONS.md`.

- **Phase 2 · parity.md — review the Electron-only rows marked ⛔.** They are internals of the Electron architecture with no user-facing equivalent to port: `stores.push`/`stores.sync` (cross-process state sync), `tabs.hideAll`/`showActive` and `ui.present`/`dismiss` (an overlay window above page views; native sheets draw above pages), and the app-UI DevTools shortcut (⌥⌘I; the app UI is native). Nothing to do unless one of them should stay; the justification is in each row of `docs/native/parity.md`.

## Done

- **Phase 5 · Google sign-in fallback and Sign out of Google dropped** (2026-10-26, your decision). Both were workarounds for Google rejecting the Electron engine, which a browser does not have; Google accepts the native engine (`native/evidence/phase-5/google.json`). The rows in `docs/native/parity.md` are marked dropped.

- **Design · Paper access** (2026-10-26). The Paper app was installed but closed; opened in the background, its local MCP server gave read access to the Pane file, and the native UI was matched to it and to the Electron app (`native/evidence/design/`, DECISIONS: Design fidelity).

- **Phase 1 · Acceptance on screen** (2026-10-26). The Mac was unlocked: `hosted` passed with a real window screenshot, the accessibility read, and Quit Pane from the menu bar (`native/evidence/phase-1/unlocked/`).
- **Phase 2 · On-screen checks** (2026-10-26). `switcher` with a real screenshot of the switcher (`switcher-screen.png`) and `shortcuts` with the page visible at each zoom step (`native/evidence/phase-2/unlocked/`). Pointer gestures remain (Open).
- **Phase 3 · On-screen checks** (2026-10-26). Every sheet and panel answered with a CUA click, spelling suggestions replacing a misspelled word (`native/evidence/phase-3/unlocked/`).

- **Phase 0 · 0.1 — Install full Xcode and select it** (2026-10-26). Xcode 27.1 installed and selected with `sudo xcode-select -s /Applications/Xcode.app/Contents/Developer`.

## Publish the first native release

Prepared on 2026-10-27, and not published. Pane 1.0.0 is on `main` since 2026-10-28 (`Release 1.0.0`, a major bump for the move off Electron, and the native work since), on Chromium 157.0.8103.15. The Electron app's last state is the `electron` branch (`Release 0.6.0`). The engine CI builds the app on is published as the `engine-0d0a01f8c703` prerelease, which the update check never sees: a prerelease is never the latest release. Installed from a dmg, the app passes the smoke scenario (`native/evidence/phase-7/dist.log`), and the release workflow passed a dry run of its build steps (`native/evidence/phase-7/cutover.txt`).

Publishing hands this build to every Electron Pane user: their update check offers v1.0.0 as soon as it is the latest release. So first try it as they will: install the app from a dmg (`native/engine/scripts/dist.sh`), run it, and do "Phase 5 · Migrate your Electron Pane" above. Then:

1. Tag `main` and push the tag. The Release workflow checks the tag against `native/app/VERSION`, builds the dmg on the `xcode-27` runner, and publishes v1.0.0:

   ```bash
   git switch main
   git pull --ff-only
   git tag v1.0.0
   git push origin v1.0.0
   ```

2. Watch it, then check that the release carries `Pane-1.0.0-arm64.dmg` and that an Electron Pane 0.6.0 offers the update. Save the run's URL to `native/evidence/phase-7/published.txt`.

   ```bash
   gh run watch $(gh run list --workflow Release --limit 1 --json databaseId --jq '.[0].databaseId')
   ```

   If the run cannot start (the `xcode-27` label) or cannot fetch the engine, publish a dmg built here from the tagged commit instead (`native/engine/scripts/dist.sh`; the one in `native/app/release` is from 2026-10-27, before the native work since): `gh release create v1.0.0 native/app/release/Pane-1.0.0-arm64.dmg --title v1.0.0 --generate-notes`.
