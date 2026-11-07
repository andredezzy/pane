# Human checklist

What only a person can do or verify: each item says why it needs one, and the steps. An item leaves this file when it is done; what it found goes in its ADR.

- **The Edit menu's Cut, Copy, and Paste.** They use the clipboard, and what is on it is yours: an agent neither reads nor overwrites it. Once: in Pane, copy a word from a page, and paste it in the address field. Its Select All and the rest of the pointer gestures ran by computer use on 0.19.0's candidate (`docs/qa/review-and-simplify/report.md`). cua-driver's background drag does not move SwiftUI's rows: a drag needs `delivery_mode: "foreground"`, which moves the pointer.

- **VoiceOver through a window, by ear.** The window's views changed for 0.19.0 (the live resize and the 23 accessibility elements match the build before, by machine), but what VoiceOver says and when is for a person to hear. Once: turn VoiceOver on, and walk a window from the sidebar to a page.

- **Mail: look for a stray draft.** A scenario opened Mail at 07:08 on 2026-10-26 with an unsent compose window to `pane-scenario@example.invalid` (a `mailto:` link, which Chrome opens without asking); Mail was quit two minutes later and nothing was sent. If Mail autosaved that window, an empty draft to that address may be in Drafts: delete it. Details in ADR 0012.

- **Pick a file through the open panel, by hand.** macOS 27 runs open and save panels out of process, and the scenarios answer them beside the panel. Once: open a page with `<input type=file>` in Pane (any upload form), choose a file in the panel, and check the page shows its name.

- **Sign in to 1Password and NordPass, and autofill a login form.** Both install from the Web Store and their toolbar buttons open their own pages (until you sign in, each opens its welcome page in a tab, as in Chrome). Signing in uses your accounts, so it is yours: in a Pane profile, sign in from each manager's button, then open `tests/pages/password.html` (served by any scenario run, or open the file) and check each one offers to fill the form.

- **Sign in to a real Google account.** Signing in uses your account, so it is yours. In a Pane profile, open `https://accounts.google.com`, sign in, and check Gmail opens signed in. If it ever shows "This browser or app may not be secure", note it here: Pane has no workaround for it (ADR 0039).

- **6.2 Sign and notarize with a Developer ID.** Signing needs an Apple Developer Program membership and its certificate, which are yours; until then every build is ad-hoc signed, and Gatekeeper warns on other Macs. The steps are built and wait for an identity (ADR 0048):
  1. In Xcode › Settings › Accounts, add your Apple ID, then Manage Certificates › + › Developer ID Application. `security find-identity -v -p codesigning` then lists "Developer ID Application: <name> (<team id>)".
  2. Store notarization credentials once, under a profile name: `xcrun notarytool store-credentials pane-notary --apple-id <apple id> --team-id <team id>` (it asks for an app-specific password from account.apple.com).
  3. Build the release: `PANE_SIGN_IDENTITY="Developer ID Application: <name> (<team id>)" PANE_NOTARY_PROFILE=pane-notary engine/scripts/dist.sh`. Each part is signed with the hardened runtime as Chrome signs it, and the dmg is signed, notarized, stapled, and assessed.
  4. Check: `spctl --assess --type open --context context:primary-signature -v app/release/Pane-<version>-arm64.dmg` says "accepted, source=Notarized Developer ID"; install the app from the dmg and `spctl -a -vv /Applications/Pane.app` says the same; run the scenario suite on the signed app (`bun tests/run.ts --binary <app>/Contents/MacOS/Pane --out <dir>`) to catch an entitlement the hardened runtime needs.
  5. Updates need nothing more: Pane installs them itself, and ADR 0092 rejected Sparkle.
  6. Passkeys: with the Developer ID, request Apple's `com.apple.developer.web-browser.public-key-credential` entitlement and add it, with a keychain access group, to `app/signing/app.entitlements` and a provisioning profile. Until then Chrome's WebAuthn dialog offers only security keys (ADR 0048).
  7. The Dock's tile for the chosen sky while Pane is closed (ADR 0089): an `NSDockTilePlugIn` in `Contents/PlugIns` waits for the Developer ID. On 2026-10-30 macOS 27's Dock refused an ad-hoc copy's ("Refusing to connect extra for '…/Pane.app' due to not being trusted for binding"), where it loads Ghostty's, signed with a Team ID. Build it then, and check it in the Dock.

- **Picture-in-picture uses Chromium's player: review.** The floating video player is Chromium's own window (the only surface that can show the video's compositor frame; AVKit's picture-in-picture plays AVFoundation media only). Nothing to do unless it should look different; the reasoning is in ADR 0022.
