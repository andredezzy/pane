# AGENTS.md

Guidance for agents (and humans) working in this repo. `CLAUDE.md` is a symlink to this file, so both toolchains read the same instructions.

## Releasing — bump the version on every dist

**Every time you build a distributable, first bump the version.** The app version lives in `apps/veil/package.json` (`version`) and is embedded in the artifact name (`Pane-${version}-${arch}.dmg`); building without a bump silently overwrites the previous release.

Pick the bump with judgement, following semver:

- **patch** (`0.1.1 → 0.1.2`) — bug fixes, small tweaks, refactors, internal changes with no new user-facing capability.
- **minor** (`0.1.1 → 0.2.0`) — new backward-compatible features.
- **major** (`0.1.1 → 1.0.0`) — breaking changes or a milestone release.

Then build and package:

```bash
# 1. Bump "version" in apps/veil/package.json (per the judgement above).
# 2. Build the app and its workspace dependencies.
bunx turbo run build --filter=@pane/veil
# 3. Package the dmg → apps/veil/release/Pane-<version>-<arch>.dmg
cd apps/veil && bun run dist
```

The dmg is ad-hoc signed and not notarized (no Apple Developer ID is configured): it runs locally and for testing, but Gatekeeper will warn on other machines. It is built for the current arch only (`arm64` on Apple Silicon).

## Tooling

- **Package manager: Bun** — a `bun.lock` is present. Use `bun` / `bunx`; never npm, pnpm, or yarn.
- Monorepo orchestrated by **Turbo**. The desktop app is `apps/veil`; shared code lives under `packages/`.
- Before committing: `bunx turbo run typecheck`, the package tests (`bun run test`), and lint (`bun run lint`) should pass.

## Design

See `DESIGN.md` for interaction and visual principles (e.g. keyboard-driven changes animate; pointer-driven changes are instant).
