# Human checklist

Items the autonomous run could not do or verify on its own: each says why it needs a person, the exact steps, and the evidence file the result should produce. Open items first; done items move to the end with the date.

## Open

- **Phase 2 · Pointer gestures on screen — run when you are away from the Mac.** Dragging profiles and tabs in the sidebar, the profile and tab context menus, the delete-profile confirmation, the profile sheet's color and group pickers, ⌃⌘F full screen, and the Edit menu (it uses the clipboard) need the real pointer (CUA moves the mouse for drags), which would fight you while you work. The rendered rows and the page in its card are now proven on screen (`evidence/phase-2/unlocked/`). Nothing to do but leave the Mac unlocked and idle; the agent runs them then.

- **Phase 3 · Mail — look for a stray draft.** A scenario opened Mail at 07:08 on 2026-10-26 with an unsent compose window to `pane-scenario@example.invalid` (a `mailto:` link, which Chrome opens without asking); Mail was quit two minutes later and nothing was sent. If Mail autosaved that window, an empty draft to that address may be in Drafts: delete it. Details in ADR 0030.

- **Phase 3 · Pick a file through the open panel, by hand.** macOS 27 runs open and save panels out of process, and the scenarios answer them beside the panel. Once: open a page with `<input type=file>` in Pane (any upload form), choose a file in the panel, and check the page shows its name.

- **Phase 4 · Sign in to 1Password and NordPass, and autofill a login form.** Both install from the Web Store and their toolbar buttons open their own pages (`evidence/phase-4/1password.png`, `nordpass.png`; until you sign in, each opens its welcome page in a tab, as in Chrome). Signing in uses your accounts, so it is yours: in a Pane profile, sign in from each manager's button, then open `tests/pages/password.html` (served by any scenario run, or open the file) and check each one offers to fill the form.

- **Phase 5 · Sign in to a real Google account.** Signing in uses your account, so it is yours. In a Pane profile, open `https://accounts.google.com`, sign in, and check Gmail opens signed in. Google accepted the engine in every run so far (`evidence/phase-5/google.json`). If it ever shows "This browser or app may not be secure", note it here: Pane no longer has a workaround for it.

- **Phase 6 · 6.2 Sign and notarize with a Developer ID.** Signing needs an Apple Developer Program membership and its certificate, which are yours; until then every build is ad-hoc signed, and Gatekeeper warns on other Macs as it did for the Electron dmg. The steps are built and wait for an identity (`evidence/phase-6/signing-dry-run.txt` shows every call they make):
  1. In Xcode › Settings › Accounts, add your Apple ID, then Manage Certificates › + › Developer ID Application. `security find-identity -v -p codesigning` then lists "Developer ID Application: <name> (<team id>)".
  2. Store notarization credentials once, under a profile name: `xcrun notarytool store-credentials pane-notary --apple-id <apple id> --team-id <team id>` (it asks for an app-specific password from account.apple.com).
  3. Build the release: `PANE_SIGN_IDENTITY="Developer ID Application: <name> (<team id>)" PANE_NOTARY_PROFILE=pane-notary engine/scripts/dist.sh 2>&1 | tee evidence/phase-6/notarized.log`. Each part is signed with the hardened runtime as Chrome signs it, and the dmg is signed, notarized, stapled, and assessed.
  4. Check: `spctl --assess --type open --context context:primary-signature -v app/release/Pane-<version>-arm64.dmg` says "accepted, source=Notarized Developer ID"; install the app from the dmg and `spctl -a -vv /Applications/Pane.app` says the same; run the scenario suite on the signed app (`bun tests/run.ts --binary <app>/Contents/MacOS/Pane --out evidence/phase-6/signed`) to catch an entitlement the hardened runtime needs. Append both outputs to `notarized.log`.
  5. Then 6.6: Sparkle can replace the manual dmg update flow.
  6. Passkeys: with the Developer ID, request Apple's `com.apple.developer.web-browser.public-key-credential` entitlement and add it, with a keychain access group, to `app/signing/app.entitlements` and a provisioning profile. Until then Chrome's WebAuthn dialog offers only security keys (ADR 0052).

- **Phase 3 · Picture-in-picture uses Chromium's player — review.** The floating video player is Chromium's own window (the only surface that can show the video's compositor frame; AVKit's picture-in-picture plays AVFoundation media only). Nothing to do unless it should look different; the reasoning is in ADR 0022.

## Done

- **Phase 5 · Your Electron Pane migrated** (2026-10-28). The native app brought your Electron data over on this Mac (`migration.json` in its data) and adopted its cookie key into "Pane Safe Storage"; the Electron app and its data were deleted since, at your request.

- **Phase 2 · The parity matrix's Electron-only rows** (2026-10-28): moot, with the Electron app retired. They were internals of its architecture (cross-process store sync, the overlay window, the app UI's DevTools shortcut); the matrix and the cutover report left the tree with the other Electron-era docs, and git history keeps them.

- **Publish the first native release** (2026-10-28, your call: every Electron Pane moves to the native app, and the Electron app is no longer supported). v0.7.0 is the latest release, built by the Release workflow on `engine-0d0a01f8c703`: https://github.com/andredezzy/pane/actions/runs/40800735660. Its local build passed `smoke`, `hosted`, `migration`, and `updates`, and `releases/latest` gives the Electron app's update check v0.7.0 and its `Pane-0.7.0-arm64.dmg`. v1.0.0, published first that day, is withdrawn, its release and tag deleted (your call: Pane is not 1.0.0 yet, ADR 0088); reinstall 0.7.0 from its dmg over the 1.0.0 in Applications, since the update check never offers an older version. The release notes walk an Electron Pane user through the move. The `electron` branch keeps the Electron app as a backup only.

- **Phase 6 · 6.5 The first CI run** (2026-10-28). The app's workflow (named Native then) builds the app on `main` in 3 min 37 s on the `xcode-27` runner, on the engine published as `engine-0d0a01f8c703` by `engine/scripts/publish-engine.sh`: https://github.com/andredezzy/pane/actions/runs/40766120708 (`evidence/phase-6/ci-first-run.txt`). If GitHub renames the `xcode-27` label (when the image leaves preview), update `runs-on` and `.github/actionlint.yaml`.

- **Phase 5 · Google sign-in fallback and Sign out of Google dropped** (2026-10-26, your decision). Both were workarounds for Google rejecting the Electron engine, which a browser does not have; Google accepts the native engine (`evidence/phase-5/google.json`).

- **Design · Paper access** (2026-10-26). The Paper app was installed but closed; opened in the background, its local MCP server gave read access to the Pane file, and the native UI was matched to it and to the Electron app (`evidence/design/`, ADR 0034).

- **Phase 1 · Acceptance on screen** (2026-10-26). The Mac was unlocked: `hosted` passed with a real window screenshot, the accessibility read, and Quit Pane from the menu bar (`evidence/phase-1/unlocked/`).
- **Phase 2 · On-screen checks** (2026-10-26). `switcher` with a real screenshot of the switcher (`switcher-screen.png`) and `shortcuts` with the page visible at each zoom step (`evidence/phase-2/unlocked/`). Pointer gestures remain (Open).
- **Phase 3 · On-screen checks** (2026-10-26). Every sheet and panel answered with a CUA click, spelling suggestions replacing a misspelled word (`evidence/phase-3/unlocked/`).

- **Phase 0 · 0.1 — Install full Xcode and select it** (2026-10-26). Xcode 27.1 installed and selected with `sudo xcode-select -s /Applications/Xcode.app/Contents/Developer`.
