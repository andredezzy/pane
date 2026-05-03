# Folder Restructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorganize `apps/desktop/src/` for better discoverability — group by domain, fix naming, enforce stores-only in `stores/`, add page-based renderer structure.

**Architecture:** File moves + renames + import rewiring. Each task is a self-contained move that leaves the codebase in a passing state. Order matters: moves that other moves depend on go first.

**Tech Stack:** TypeScript, Electron, React, Zustand

---

## Task 1: Move non-store files out of `stores/`

Move `profile-colors.ts` to `constants/`, `serialize.ts` to `stores/middlewares/`, `default-fingerprints.ts` to `renderer/components/`.

**Files:**
- Move: `src/stores/profile-colors.ts` → `src/constants/profile-colors.ts`
- Move: `src/stores/serialize.ts` → `src/stores/middlewares/serialize.ts`
- Move: `src/stores/default-fingerprints.ts` → `src/renderer/components/default-fingerprints.ts`
- Update all import paths in consumers

## Task 2: Move `store-sync.ts` to `stores/middlewares/`

**Files:**
- Move: `src/main/store-sync.ts` → `src/stores/middlewares/store-sync.ts`
- Update import in `src/main/index.ts`

## Task 3: Rename and move extension files

- Rename `pane-extensions.ts` → `extension-installer.ts`, class `PaneExtensions` → `ExtensionInstaller`
- Rename `profile-extensions.ts` → `extension-loader.ts`, class `ProfileExtensions` → `ExtensionLoader`
- Move `extension-loader.ts` + `profile.ts` + `profile-tabs.ts` into `main/profile/`
- Move `browser/detect-browser.ts` → `main/detect-browser.ts`, delete `browser/`
- Update all imports and references

## Task 4: Split `main/index.ts` into `index.ts` + `window.ts` + `protocol.ts`

Extract window creation/menu/resize into `window.ts` and the `pane-extension://` protocol handler into `protocol.ts`. `index.ts` becomes a thin orchestrator.

## Task 5: Create page-based renderer structure

- Create `pages/browser/index.tsx` extracting browser content from `app.tsx`
- Move address-bar files + `empty-state.tsx` into `pages/browser/_components/`
- Create `pages/settings/index.tsx` from `settings-page.tsx`
- Move `extension-settings.tsx` into `pages/settings/_components/`
- Update `app.tsx` to import from pages

## Task 6: Final verification

Run full quality gate.
