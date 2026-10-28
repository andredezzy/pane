# No Google sign-in workaround

Pane has no workaround for Google rejecting its engine (the user's decision): Google accepts the native engine, with and without a fingerprint, and no browser has one. The Electron app's fallback was a workaround for the Electron engine, and it is gone: a real Chrome to sign in, a session transfer page, and Sign out of Google in the profile menu. Settings › Browser, which held the Chrome path for it, went with it. The `google` scenario opens Google's sign-in in a profile without a fingerprint and in one with: each loads the identifier page, never `/rejected`.

A native version of the fallback, built in the same session (2026-10-26), was removed before it shipped. What stays of it is the engine's read-only cookie API (`engine/pane/public/PaneCookies.h`), which scenarios use and which also gives tab sleep its cache size and trim (ADR 0041). Browser sign-in itself is off per profile; signing in to Google's websites is unaffected (ADR 0040).
