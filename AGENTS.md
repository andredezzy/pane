# AGENTS.md

Guidance for agents (and humans) working in this repo.

## Releasing — bump the version, then push a tag

**Every release starts with a version bump.** The app version lives in `packaging/VERSION`, its only source, and is embedded in the artifact name (`Pane-<version>-arm64.dmg`). CI refuses to publish unless the pushed tag equals `v<version>`, and a local build without a bump silently overwrites the previous artifact.

Pick the bump with judgement, following semver:

- **patch** (`0.1.1 → 0.1.2`) — bug fixes, small tweaks, refactors, internal changes with no new user-facing capability, and Chromium security upgrades.
- **minor** (`0.1.1 → 0.2.0`) — new backward-compatible features.
- **major** (`0.1.1 → 1.0.0`) — breaking changes or a milestone release.

**Publishing is driven by tags.** GitHub Actions (`.github/workflows/release.yml`) publishes a release whenever a `v*` tag on `main` is pushed, or on `swift` (below); a tag on any other branch fails:

```bash
# 1. Bump packaging/VERSION (per the judgement above).
# 2. Commit the bump.
git commit -am "Release <version>"
# 3. Tag it — the tag MUST equal v<version>, or CI fails fast.
git tag "v<version>"
# 4. Push the branch and the tag.
git push && git push origin "v<version>"
```

CI verifies the tag matches `packaging/VERSION` and is on `main` or `swift`. It then packages the app from the engine archive that `chromium/prebuilt-engines.tsv` names for the tagged commit, makes the dmg, and creates the GitHub Release for the tag, with generated notes and the `Pane-<version>-arm64.dmg` the in-app update check reads. CI never builds Chromium. Pane's code, its UI included, is part of the engine (`chromium/`'s `CHROMIUM_VERSION`, `args`, or `patches`, and `pane/`), so a release whose engine changed is built here first and published with `scripts/publish-engine.sh`: its archive becomes the `engine-<key>` prerelease, and its line goes in `prebuilt-engines.tsv` (`chromium/UPGRADING.md`, step 6).

**The Swift line ships from `swift`, the one exception to working and tagging on `main`,** until `main`'s first release (ADR 0124, D1). A fix the Swift app still takes, for data loss or security only, goes on the `swift` branch, cut from `v0.19.1`, whose engine archive is `46b8f8b35c24`, and is released from there with a tag on `swift`, by the steps above; `main` takes the same fix in its own code where that code has the defect too.

**Building locally is for testing.** To produce a distributable on your own machine without cutting a release:

```bash
# 1. Bump packaging/VERSION (per the judgement above).
# 2. Build the engine (docs/building.md), or fetch the prebuilt one:
export PANE_ENGINE_OUT="$(scripts/fetch-engine.sh)"
# 3. Package the dmg → packaging/release/Pane-<version>-arm64.dmg
scripts/dist.sh
```

The dmg is ad-hoc signed and not notarized (no Apple Developer ID is configured): it runs locally and for testing, but Gatekeeper will warn on other machines. With a Developer ID, `PANE_SIGN_IDENTITY` and `PANE_NOTARY_PROFILE` sign and notarize it (`docs/human-checklist.md`).

## Tooling

- **Pane is its own build of Chromium**: its code is `pane/` (`//pane`), C++ with Views, its UI in `pane/ui` and its data and rules in `pane/model`, linked into `Pane Framework.framework`; `chromium/` holds the pinned tag, its args, and its patches. One code path for every OS (ADR 0124): what differs goes in a header's platform files (`<thing>_mac.mm`, `<thing>_win.cc`) or `BUILD.gn`, the only places that test for the OS, and Objective-C++ only where the OS leaves no choice. `packaging/` holds what packaging reads: `VERSION`, the icon, the dmg's resources, and the entitlements. `docs/building.md` covers the checkout, the build, the patches, and the scenarios. Chromium upgrades follow `chromium/UPGRADING.md`.
- **The engine stays as faithful to Chromium as it can be** (ADR 0004): a change goes in `//pane` first, through a seam Chromium already has second, and into a Chromium file only when neither can do it.
- **The product's words** are in `CONTEXT.md`: use its terms, and avoid what it says to avoid, in code, docs, and what Pane shows.
- **Package manager: Bun** — a `bun.lock` is present. Use `bun` / `bunx`; never npm, pnpm, or yarn. Bun runs the scenario runner (`tests/`) and the linters.
- Before committing: `bun run lint`, `bun run typecheck` (the scenario runner), `bun run knip`, and `scripts/check-os-flags.sh` should pass. `bun run lint` fixes what it can, so a fix it made goes in the commit too: Gates runs `bunx eslint tests`, `bunx biome ci .`, and the OS-flag check, which fix nothing.
- A change to `pane/` or `chromium/`, the UI's included, also passes what CI cannot run, since it never builds Chromium: `clang-format` over the files it touches, run from the checkout (`~/chromium/src/pane/…`) so Chromium's `.clang-format` applies; `gn format` on the `BUILD.gn` files it touches; `gn check out/Release '//pane/*'` and `gn check out/Win '//pane/*'`; `scripts/compile-windows.sh`, which compiles the shared code for Windows; `bun run test`, which builds and runs `pane_unittests` (`docs/building.md`, Engine unit tests); and the scenarios it touches, on a build of it (`docs/building.md`, Scenarios). Chromium's build treats every warning in `//pane` as an error, where PaneKit had a strict build of its own. PaneKit's sanitized runs went with it: their successor, an AddressSanitizer out dir with `dcheck_always_on` building a test target of `pane/model` alone, is K2's in `docs/plans/views-migration.md` and not set up yet; ThreadSanitizer doesn't build on the Mac. CI runs no scenarios: before a push or a tag, run these on this Mac.
- **QA drives only a test copy.** Start Pane through the scenario harness, on throwaway data, never `/Applications` or the user's data root. Real keys and clicks go only to that copy, proven frontmost right before each one: never to Finder, a terminal, or any app holding the user's windows, even for a throwaway folder. `screencapture -l` takes only a window whose title or owner proves it is the test's. To see how a file looks from another app, use `strings`, `sqlite3`, and `file` in a shell, which send no input.
- **The Electron app is retired**: 0.7.0 moved its users to the native app. Its last state is the `electron` branch, kept as a backup and not maintained; comments that cite `apps/veil` or `packages/` point there. Work on `main`.

## Decisions

Record each decision as an ADR in `docs/adr/`, one record per subject. A decision on a subject that has a record goes into that record, with what it changed and why, so the record reads as the subject's whole decision. A new subject takes the next number: the decision as the title, then why, what was rejected, and how it is checked, in the form of the others. A decision that changes another record's subject says so at the top of that record (as 0092 does) rather than rewriting it away. Keep `docs/adr/README.md`'s index in step; a merged record's number stays taken, listed there under Merged records.

## Design

**Every design is proposed in Paper before it is implemented.** Paper is the design source (ADR 0058): any change to what Pane shows, a screen, a control, an icon, or the dmg's window, is drawn there first, as a new board or a change to the board it touches, and shown to the user. Implementation starts only once the user has approved it in Paper, and then matches the board.

Paper's desktop app serves an MCP over streamable HTTP at `http://127.0.0.1:29979/mcp` while it runs. Where no client is set up for it, call it with `curl`: `initialize`, keep the `mcp-session-id` header, send `notifications/initialized`, then `tools/call`. Replies come as SSE `data:` lines, and every file tool needs `fileId`. Start with `get_guide`, topic `paper-mcp-instructions`. `paper-gen://` image generation spends the user's usage, so use it only when the user asks for art.

See `DESIGN.md` for interaction and visual principles (e.g. a selection moves instantly; hover and enter/exit animate).
