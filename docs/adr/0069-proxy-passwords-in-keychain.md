# Proxy passwords are never kept in the clear, and ask the Keychain nothing of their own

> **Partly replaced by [ADR 0113](0113-profile-data-encrypted-under-a-key-the-pin-releases.md)** — the passwords are in `profiles.json`, sealed with it under the data key. The Keychain item and the sealed file this record describes are read once, when an earlier Pane's plain `profiles.json` is, then deleted; and Pane makes the engine's Keychain item when it has none and no data key is sealed yet.

A profile's proxy password is never kept in plain text in `profiles.json`, which writes it as null. The password in the file was the Electron app's format; the review's safety track raised it (safety A3). It took two steps before ADR 0113 sealed the whole file.

A Keychain item of their own (2026-10-27):

- **One generic password, "Pane Proxy Passwords", held them all** as JSON by profile id, so macOS asked at most once after an update of an ad-hoc signed build (ADR 0048), not once per profile. Its account is the data directory's path, so a Pane started on another data directory keeps its own (and the former identifier's path, for a data root ADR 0114 moved).
- **It was read at launch only when a proxy signs in** (a username, or a password the file still held), since macOS may ask first, and written only when a password changed, on the save queue (ADR 0068), before the file: a password left `profiles.json` only once the store held it. A password the file still held, the Electron app's or an older Pane's, was read as it was, won over the store, and moved at the first save.
- **If the store could not be read or written** (the user denied it), the passwords stayed in the file for that session rather than be lost, and the store was not asked again until the next launch.

Sealed under the engine's key (2026-10-29), the user's call, after every update asked for the login keychain's password twice, once for "Pane Safe Storage" and once for "Pane Proxy Passwords":

- **Each item names the builds that may read it:** an ad-hoc signature carries no Team ID, so macOS names each build by its code hash, and a new build asks once per item.
- **One answer covers both:** the engine reads "Pane Safe Storage" at every launch, and Pane is the same process, so one Always Allow covers the passwords too. An update asks once. ADR 0113 seals the data key under the same secret, for the same reason.
- **The passwords moved to `proxy-passwords.json`** in the data directory, sealed with AES-GCM under a key HKDF-SHA256 derives from the engine's secret. PaneKit's `ProxyPasswordSeal` seals and opens them, and `ProxyPasswordVault` decides where they are read from; the app gives it the Keychain and the file (`app/Pane/Security/ProxyPasswordStore.swift`, `SafeStorage.engineSecret`).
- **The first launch after it** read the old item once more, its last prompt, sealed what it held, and deleted it. Before the engine had a key, in a fresh install's first moments, the passwords went to the old item and were sealed at a later launch. A key the user denied, or one that did not open the box, left the passwords where they were, and the box stayed for its key.

The panic wipe deletes the sealed file and the old item, the item without asking (ADR 0042). Under `--use-mock-keychain` the store is `mock-keychain.json` in the data directory, so scenarios never touch the login Keychain (ADR 0012).

What was rejected:

- **Deleting the engine's "Pane Safe Storage" key in the panic wipe:** that key is not scoped by data directory, so a wipe in a Pane on a throwaway data directory would sign the real one out.
- **A certificate of Pane's own, to keep both items' grants across updates:** tried on 2026-10-29 and undone the same day. The partition list macOS keeps on each item (its `partition_id` entry) names an app without a Team ID by its code hash (`cdhash:`), whatever certificate signs it: a test item made through the calls Chromium makes refused the second build of a tool signed with the same certificate (`-25293`), as it refused an ad-hoc one. Developer ID browsers' items (Dia, Aside) carry `teamid:`, which updates keep; a Developer ID would end the prompt altogether (ADR 0048), and it is the user's call for later.
- **Keeping the engine's key out of the Keychain:** the cookies' encryption would no longer keep them from other apps.
- **Pane making the engine's item when it has none:** a migration would then find a key and not hand over the Electron app's (ADR 0043). ADR 0113 makes it once the migration has run.

How it is checked:

- **PaneKit's tests:** the box opens with its secret alone, and not once changed; an earlier Pane's box opens with the engine's key, and its item reads too; a denied key, or another key's box, reads nothing; and once the profiles are sealed with them, neither the box nor the item stays. Until ADR 0113 they also checked that the vault moved the item's passwords into a box, and kept the item before the engine had a key.
- **Scenarios**, on the mock keychain: `proxy` checks that no password is left in `profiles.json` in the clear, and that the Electron app's plain password signs in without a prompt, moves, and signs in again after a relaunch; `profile-sheet` checks the same for a proxy saved from the sheet. `proxy-auth`, `security`, `migration`, `smoke`, and `restore` pass with it.
- **What only the login Keychain shows**, one prompt per update, was the user's to see at the next update.
