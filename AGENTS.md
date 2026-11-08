# AGENTS.md

Guidance for agents (and humans) working in this repo.

## Releasing — bump the version, then push a tag

**Every release starts with a version bump.** The app version lives in `app/VERSION`, its only source, and is embedded in the artifact name (`Pane-<version>-arm64.dmg`). CI refuses to publish unless the pushed tag equals `v<version>`, and a local build without a bump silently overwrites the previous artifact.

Pick the bump with judgement, following semver:

- **patch** (`0.1.1 → 0.1.2`) — bug fixes, small tweaks, refactors, internal changes with no new user-facing capability, and Chromium security upgrades.
- **minor** (`0.1.1 → 0.2.0`) — new backward-compatible features.
- **major** (`0.1.1 → 1.0.0`) — breaking changes or a milestone release.

**Publishing is driven by tags.** GitHub Actions (`.github/workflows/release.yml`) publishes a release whenever a `v*` tag on `main` is pushed, or on `swift` (below); a tag on any other branch fails:

```bash
# 1. Bump app/VERSION (per the judgement above).
# 2. Commit the bump.
git commit -am "Release <version>"
# 3. Tag it — the tag MUST equal v<version>, or CI fails fast.
git tag "v<version>"
# 4. Push the branch and the tag.
git push && git push origin "v<version>"
```

CI verifies the tag matches `app/VERSION` and runs PaneKit's tests. It then builds the app on the prebuilt engine that `engine/prebuilt-engines.tsv` names for the tagged commit, packages the dmg, and creates the GitHub Release for the tag, with generated notes and the `Pane-<version>-arm64.dmg` the in-app update check reads. CI never builds Chromium. When a release changes the engine (`engine/`: `CHROMIUM_VERSION`, `args`, `patches`, `pane`), build it locally first and publish it with `engine/scripts/publish-engine.sh`: its archive becomes the `engine-<key>` prerelease, and its line goes in `prebuilt-engines.tsv` (`engine/UPGRADING.md`, step 6).

**The Swift line ships from `swift`, the one exception to working and tagging on `main`.** `main` is the Views line (`docs/plans/views-migration.md` there, D1) until its first release. This branch, cut from `v0.19.1`, whose engine archive is `46b8f8b35c24`, takes the fixes the Swift app still needs, for data loss or security only; it is released with a tag on `swift`, by the steps above, and each fix goes into `main` as well while `main` still holds the code it fixes.

**Building locally is for testing.** To produce a distributable on your own machine without cutting a release:

```bash
# 1. Bump app/VERSION (per the judgement above).
# 2. Build the engine (docs/building.md), or fetch the prebuilt one:
export PANE_ENGINE_OUT="$(engine/scripts/fetch-engine.sh)"
# 3. Package the dmg → app/release/Pane-<version>-arm64.dmg
engine/scripts/dist.sh
```

The dmg is ad-hoc signed and not notarized (no Apple Developer ID is configured): it runs locally and for testing, but Gatekeeper will warn on other machines. With a Developer ID, `PANE_SIGN_IDENTITY` and `PANE_NOTARY_PROFILE` sign and notarize it (`docs/human-checklist.md`).

## Tooling

- **The app is native**: Swift (AppKit and SwiftUI) in `app/`, on Pane's own Chromium framework built from `engine/`. `docs/building.md` covers the checkout, the engine build, the patches, and the scenarios. Chromium upgrades follow `engine/UPGRADING.md`.
- **The engine stays as faithful to Chromium as it can be** (ADR 0004): a change goes in `//pane` first, through a seam Chromium already has second, and into a Chromium file only when neither can do it.
- **The product's words** are in `CONTEXT.md`: use its terms, and avoid what it says to avoid, in code, docs, and what Pane shows.
- **Package manager: Bun** — a `bun.lock` is present. Use `bun` / `bunx`; never npm, pnpm, or yarn. Bun runs the scenario runner (`tests/`) and the linters.
- Before committing: `bun run lint`, `bun run typecheck` (the scenario runner), `bun run test` (PaneKit's `swift test`), and `bun run knip` should pass. `bun run lint` fixes what it can, so a fix it made goes in the commit too: Gates runs `bunx eslint tests` and `bunx biome ci .`, which fix nothing. A change to the app or the engine also runs the scenarios it touches (`docs/building.md`, Scenarios). A change to the engine also runs `pane_unittests` (`docs/building.md`, Engine unit tests), which CI cannot, since it never builds Chromium.
- Before a push or a tag, also run in `app/PaneKit` what the App workflow (`.github/workflows/app.yml`) runs beyond those: `swift build -Xswiftc -warnings-as-errors --scratch-path .build-strict`, `swift test --sanitize=thread --scratch-path .build-tsan`, and `swift test --sanitize=address --scratch-path .build-asan`. A deprecation once passed every check above and failed only the strict build, which turned App red on a release's tag.
- **QA drives only a test copy.** Start Pane through the scenario harness, on throwaway data, never `/Applications` or the user's data root. Real keys and clicks go only to that copy, proven frontmost right before each one: never to Finder, a terminal, or any app holding the user's windows, even for a throwaway folder. `screencapture -l` takes only a window whose title or owner proves it is the test's. To see how a file looks from another app, use `strings`, `sqlite3`, and `file` in a shell, which send no input.
- **The Electron app is retired**: 0.7.0 moved its users to the native app. Its last state is the `electron` branch, kept as a backup and not maintained; comments that cite `apps/veil` or `packages/` point there. Work on `main`.

## Decisions

Record each decision as an ADR in `docs/adr/`, one record per subject. A decision on a subject that has a record goes into that record, with what it changed and why, so the record reads as the subject's whole decision. A new subject takes the next number: the decision as the title, then why, what was rejected, and how it is checked, in the form of the others. A decision that changes another record's subject says so at the top of that record (as 0092 does) rather than rewriting it away. Keep `docs/adr/README.md`'s index in step; a merged record's number stays taken, listed there under Merged records.

## Design

**Every design is proposed in Paper before it is implemented.** Paper is the design source (ADR 0058): any change to what Pane shows, a screen, a control, an icon, or the dmg's window, is drawn there first, as a new board or a change to the board it touches, and shown to the user. Implementation starts only once the user has approved it in Paper, and then matches the board.

Paper's desktop app serves an MCP over streamable HTTP at `http://127.0.0.1:29979/mcp` while it runs. Where no client is set up for it, call it with `curl`: `initialize`, keep the `mcp-session-id` header, send `notifications/initialized`, then `tools/call`. Replies come as SSE `data:` lines, and every file tool needs `fileId`. Start with `get_guide`, topic `paper-mcp-instructions`. `paper-gen://` image generation spends the user's usage, so use it only when the user asks for art.

See `DESIGN.md` for interaction and visual principles (e.g. a selection moves instantly; hover and enter/exit animate).
