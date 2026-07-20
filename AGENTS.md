# AGENTS.md

Guidance for agents (and humans) working in this repo. `CLAUDE.md` is a symlink to this file, so both toolchains read the same instructions.

## Releasing — bump the version, then push a tag

**Every release starts with a version bump.** The app version lives in `apps/veil/package.json` (`version`) and is embedded in the artifact name (`Pane-${version}-${arch}.dmg`). CI refuses to publish unless the pushed tag equals `v<version>`, and a local build without a bump silently overwrites the previous artifact.

Pick the bump with judgement, following semver:

- **patch** (`0.1.1 → 0.1.2`) — bug fixes, small tweaks, refactors, internal changes with no new user-facing capability.
- **minor** (`0.1.1 → 0.2.0`) — new backward-compatible features.
- **major** (`0.1.1 → 1.0.0`) — breaking changes or a milestone release.

**Publishing is driven by tags.** GitHub Actions (`.github/workflows/release.yml`) publishes a release whenever a `v*` tag is pushed:

```bash
# 1. Bump "version" in apps/veil/package.json (per the judgement above).
# 2. Commit the bump.
git commit -am "Release <version>"
# 3. Tag it — the tag MUST equal v<version>, or CI fails fast.
git tag "v<version>"
# 4. Push the branch and the tag.
git push && git push origin "v<version>"
```

CI verifies the tag matches `apps/veil/package.json`, runs the quality gates once (`bunx turbo run typecheck` and the `apps/veil` tests), then builds and packages every platform (macOS arm64 + x64, Windows, Linux) and creates the GitHub Release for the tag — generated notes, installers attached, including the `Pane-<version>-arm64.dmg` the in-app updater reads.

**Building locally is for testing.** To produce a distributable on your own machine without cutting a release:

```bash
# 1. Bump "version" in apps/veil/package.json (per the judgement above).
# 2. Build the app and its workspace dependencies.
bunx turbo run build --filter=@pane/veil
# 3. Package the dmg → apps/veil/release/Pane-<version>-<arch>.dmg
cd apps/veil && bun run dist
```

The dmg is ad-hoc signed and not notarized (no Apple Developer ID is configured): it runs locally and for testing, but Gatekeeper will warn on other machines. `bun run dist` packages the current arch only (`arm64` on Apple Silicon) — the full multi-platform matrix runs only in CI.

## Tooling

- **Package manager: Bun** — a `bun.lock` is present. Use `bun` / `bunx`; never npm, pnpm, or yarn.
- Monorepo orchestrated by **Turbo**. The desktop app is `apps/veil`; shared code lives under `packages/`.
- Before committing: `bunx turbo run typecheck`, the package tests (`bun run test`), and lint (`bun run lint`) should pass.

## Design

See `DESIGN.md` for interaction and visual principles (e.g. keyboard-driven changes animate; pointer-driven changes are instant).
