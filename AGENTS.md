# AGENTS.md

Guidance for agents (and humans) working in this repo. `CLAUDE.md` is a symlink to this file, so both toolchains read the same instructions.

## Releasing — bump the version, then push a tag

**Every release starts with a version bump.** The app version lives in `app/VERSION`, its only source, and is embedded in the artifact name (`Pane-<version>-arm64.dmg`). CI refuses to publish unless the pushed tag equals `v<version>`, and a local build without a bump silently overwrites the previous artifact.

Pick the bump with judgement, following semver:

- **patch** (`0.1.1 → 0.1.2`) — bug fixes, small tweaks, refactors, internal changes with no new user-facing capability, and Chromium security upgrades.
- **minor** (`0.1.1 → 0.2.0`) — new backward-compatible features.
- **major** (`0.1.1 → 1.0.0`) — breaking changes or a milestone release.

**Publishing is driven by tags.** GitHub Actions (`.github/workflows/release.yml`) publishes a release whenever a `v*` tag on `main` is pushed (a tag on any other branch fails):

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
- **Package manager: Bun** — a `bun.lock` is present. Use `bun` / `bunx`; never npm, pnpm, or yarn. Bun runs the scenario runner (`tests/`) and the linters.
- Before committing: `bun run lint`, `bun run typecheck` (the scenario runner), `bun run test` (PaneKit's `swift test`), and `bun run knip` should pass. A change to the app or the engine also runs the scenarios it touches (`docs/building.md`, Scenarios).
- **The Electron app is retired**: 1.0.0 moved its users to the native app. Its last state is the `electron` branch, kept as a backup and not maintained; comments that cite `apps/veil` point there. Work on `main`.

## Decisions

Record each decision as an ADR in `docs/adr/`, one file per decision: the next number, the decision as the title, then why, what was rejected, and how it is checked, in the form of the others. A decision that replaces an earlier one says so at the top of the old record (as 0003 does) rather than rewriting it away. Add the new record to `docs/adr/README.md`.

## Design

See `DESIGN.md` for interaction and visual principles (e.g. keyboard-driven changes animate; pointer-driven changes are instant).
