# Passkeys reach security keys only, until Apple's browser entitlement

Passkeys in Pane reach security keys only, since iCloud Keychain passkeys need what Apple grants only with a Developer ID. Chrome's WebAuthn dialog shows over the page, a child window of Pane's above it, but offers only a security key. iCloud Keychain needs Apple's `com.apple.developer.web-browser.public-key-credential` entitlement, and the Touch ID authenticator a keychain access group, both granted only with a Developer ID and a provisioning profile (ADR 0048). The Electron app had no WebAuthn at all.

Revisit with the Developer ID: request the entitlement and add it, with a keychain access group, to `app/signing/app.entitlements` and a provisioning profile (`docs/human-checklist.md`, 6.2).
