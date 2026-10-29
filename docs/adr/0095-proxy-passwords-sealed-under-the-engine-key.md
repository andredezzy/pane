# Proxy passwords are sealed under the engine's key

Proxy passwords are kept in the data directory, in `proxy-passwords.json`, no longer in a Keychain item of their own. They are sealed with AES-GCM under a key that HKDF-SHA256 derives from the engine's "Pane Safe Storage" secret. It was the user's call, after every update asked for the login keychain's password twice: once for "Pane Safe Storage" and once for "Pane Proxy Passwords" (ADR 0069).

Why an update asks:

- **Each item names the builds that may read it:** an ad-hoc signature carries no Team ID, so macOS names each build by its code hash, and a new build asks once per item.
- **One answer now covers both:** the engine reads "Pane Safe Storage" at every launch, and Pane is the same process, so one Always Allow covers the passwords too. An update now asks once.

PaneKit's `ProxyPasswordSeal` seals and opens the passwords, and `ProxyPasswordVault` decides where they are read from and written to. The app gives it the Keychain and the file: `app/Pane/Security/ProxyPasswordStore.swift` and `SafeStorage.engineSecret`.

- **The first launch after this change:** it reads the old item once more, which is its last prompt, seals what it holds, and deletes it.
- **Before the engine has a key**, in a fresh install's first moments, the passwords go to the old item and are sealed at a later launch. Pane never makes the engine's item: a migration hands the Electron app's key to it only while it does not exist (ADR 0043).
- **A key the user denies, or one that does not open the box:** the passwords stay where they are, as ADR 0069 left them, and the box stays for its key.
- **The panic wipe** deletes the file with the data directory, and the old item without asking (ADR 0042).
- **Under `--use-mock-keychain`**, the store is still `mock-keychain.json` (ADR 0013).

What was rejected:

- **A certificate of Pane's own, to keep both items' grants across updates:** it was tried on 2026-10-29 and undone the same day.
  - The partition list macOS keeps on each item (its `partition_id` entry) names an app without a Team ID by its code hash (`cdhash:`), whatever certificate signs it.
  - A test item in the login keychain, made through the calls Chromium makes, refused the second build of a tool signed with the same certificate (`-25293`), as it refused an ad-hoc one.
  - The items of Developer ID browsers (Dia, Aside) carry `teamid:` instead, which updates keep. A Developer ID would end the prompt altogether (ADR 0048), and it is the user's call for later.
- **Keeping the engine's key out of the Keychain:** the cookies' encryption would no longer keep them from other apps.
- **Pane making the engine's item when it has none:** a migration would then find a key and not hand over the Electron app's (ADR 0043).

How it is checked:

- **PaneKit's tests:**
  - The box opens with its secret alone, and not once changed.
  - The vault writes and reads sealed, moves the old item's passwords into a box and deletes the item, and keeps the item before the engine has a key.
  - A denied key leaves the passwords where they are, another key's box reads as nothing, and nothing is left once the passwords go.
- **Scenarios:** `proxy`, `proxy-auth`, `profile-sheet`, `security`, `migration`, `smoke`, and `restore` pass, on the mock keychain, so the login Keychain stays untouched.
- **What only the login Keychain shows**, one prompt per update, is the user's to see at the next update.
