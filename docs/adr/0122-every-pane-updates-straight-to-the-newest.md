# Every Pane updates straight to the newest, which carries every upgrade path itself

A Pane of any earlier version updates straight to the newest release, and that release brings its data over. Nothing is installed first, nothing is refused, and no release is stepped through. It was the user's call (2026-11-07), and it reverses what this record first decided, before that shipped: that Pane would upgrade only from the last 0.18.x.

Every Pane published since 0.3.0 asks GitHub for one thing, `releases/latest`, and offers what it finds (ADR 0002, ADR 0051). What a release does about older data is the only thing left to decide once it is the latest, because what those Panes do can't change.

## What the newest does with each

| Coming from | What it does | Checked by |
|---|---|---|
| Electron, up to 0.6.0 | Copies its data over at the first launch (`LaunchMigration`), the Electron app's folder untouched | `migration` |
| 0.7.0 to 0.13.1 | Moves the former identifier's folders to this one's, moves plain profile folders into the volume (07.5), and seals a plain `profiles.json`, with the proxy passwords kept apart | `identifier-move`, `security`, `proxy` |
| A PIN of 0.13.1 or before, never unlocked since | Checks its bcrypt hash once, then seals the data key with the PIN | `security` |
| 0.14.0 to 0.17.2 | Opens the volume on the former passphrase and changes it, then encrypts each profile's files behind the window, showing 07.5 past a second if the window waits for one | `files-migration` |
| 0.18.0 and 0.18.1 | Puts back the stores 0.18.0 moved, once per profile, from its safety copy (ADR 0121) | `files-migration` with `PANE_REPAIR_FROM` |
| 0.18.2 and later | Opens | every scenario |

**Who drags Pane once.** From 0.14.0 a Pane installs its update itself. Before it, the update's other identifier stops that (ADR 0114): those Panes download the dmg and open it, and the user drags Pane to Applications once. Nothing here can change that, since it is what they already do.

## What it costs

The code stays, with its tests, against Chromium's changes too: `engine/UPGRADING.md` runs `files-migration` from a published build at every upgrade. Dropping the paths would have taken about 3,000 lines of Swift, 2,500 of the engine's, 1,900 of tests, 1,085 of C, and `electron` with 66 of the lockfile's 244 packages. The review's targets for those measures (Swift 26,000, engine sources 15,000, TypeScript 19,500, C 0) counted on it, and no longer hold.

Launch does what 0.18.2 does: each path is a look at a file or two when there is nothing to move.

## What was rejected

- **Upgrading only from the last 0.18.x,** this record's first decision (2026-11-06), built, tested, and never published. The update check offers only the newest, so whoever hadn't reached 0.18.2 yet would have gone past it: from 0.18.0 and 0.18.1 past its repair, whose safety copy goes at the next open, with nothing saying so. From 0.14.0 to 0.17.2 they stopped at 07.8 until they found a dmg, and earlier ones started empty, their data unread. The user asked about those users before it went out.
- **A bridge release, kept as GitHub's latest:** a 0.18.3 every earlier Pane lands on, which alone reads the list of releases and so alone sees the next. It would leave the code out of the newest, and nothing could skip the repair, but it takes two updates, keeps GitHub's Latest label on an old release for good, and depends on a chain where this depends on one release. The user chose the release that needs nothing else.
- **The newest stepping back to the floor by itself:** code that downloads an old Pane, swaps down, and swaps up again, running on the oldest data, where a wrong guess about that data loops.
- **Holding the release for a week,** so updates reach 0.18.2 first: it serves only who opens Pane that week, and keeps everyone on the older Chromium.
- **A line on 07.8 naming the version to install:** 07.8 goes back to meaning a volume that won't open, and nothing old reaches it.

## How it is checked

- **`files-migration`,** with `PANE_EARLIER_BUILDS` naming published builds: each writes a rich session, this Pane converts it, and everything reads back, cookies included, with nothing plain on disk, through quits, a kill, and a bad file. With `PANE_REPAIR_FROM`, what 0.18.0 left is put right.
- **`migration`,** on data written by the Electron the retired app shipped. **`identifier-move`:** the former identifier's folders, moved. **`security`:** a PIN as bcrypt, a damaged `security.json`, and the wipe, the Electron app's data included. **`proxy`:** a password from the Electron app's `profiles.json`.
- **`update-install`:** the swap itself, against a stand-in release. The two halves meet only in the real update from published builds, against GitHub, after the release: if it fails, `gh release edit` puts the latest back on the release before it at once.
