# Lock Screen with PIN Protection

## Overview

Opt-in lock screen that appears after the splash screen when a PIN is configured. Full-width content panel with an iOS-style numpad (minimal outline circles). Anti-forensics wipe triggers after 5 failed attempts — overwrites all profile data with random bytes before deletion.

## Data Model

New `security-store.ts` persisted to `userData/security.json` via `fsStorage` + `sync` middleware.

```typescript
interface SecurityState {
  pin: {
    hash: string;   // bcrypt hash, never plaintext
    length: number;  // number of dots to render
  } | null;
  failedAttempts: number;  // persisted immediately on each failure (no debounce)
  isLocked: boolean;       // true on boot if pin is set
}
```

- `pin` is `null` when no PIN is configured — lock screen is skipped entirely
- `failedAttempts` bypasses the 300ms fsStorage debounce and writes to disk immediately to prevent force-quit bypass
- `isLocked` is set to `true` during rehydration if `pin !== null`
- PIN hashing uses `bcryptjs` (pure JS, no native dependencies)

## Lock Screen UI

### Component: `LockScreen` (`renderer/app/lock-screen/page.tsx`)

### Layout

- `Layout` checks `securityStore.isLocked` — if `true`, renders a full-width `ContentPanel` with `LockScreen` centered inside
- No sidebar, no toolbar visible — content panel extends edge-to-edge with the window's 8px padding
- Traffic lights remain visible (native window chrome)

### Numpad

- Minimal outline style: 64px circles, border-radius 50%, 1px border at `rgba(255,255,255, 0.12)`, font-weight 200, font-size 26px
- Grid layout: 3 columns — digits 1-9, empty cell, 0, backspace (thin SVG icon, stroke-width 1.2)
- Subtle brightness increase on hover/active
- Gap: 14px between buttons

### PIN Dots

- Row of circles matching `pin.length` (flexible — user chooses any length during setup)
- Empty dot: 13px, 1.5px outline at `rgba(255,255,255, 0.25)`
- Filled dot: 13px, solid white
- Auto-submits when all dots are filled (no submit button)

### Input

- Numpad click input
- Keyboard input: `useEffect` with `keydown` listener for digits 0-9 and Backspace

### Feedback Animations

- **Wrong PIN:** dots shake horizontally (CSS keyframe), then clear after 600ms
- **Correct PIN:** dots briefly scale up, then `isLocked` flips to `false`

### Attempts Warning

- Hidden until after the first failure
- Shows "N attempts remaining" starting from attempt 2
- Style: `rgba(255,255,255, 0.3)`, 13px, font-weight 300
- Last attempt: text turns red — "Last attempt. All data will be erased."

## Anti-Forensics Wipe

Triggers on the 5th failed attempt. Runs in the main process via tRPC mutation `security.wipe`.

### Wipe Sequence

1. **Overwrite profile session data** — for each profile, get the `session.fromPartition("persist:profile-{id}")` storage path, overwrite all files with `crypto.randomBytes` (3 passes), then delete
2. **Overwrite `userData/profiles.json`** — 3 passes of random bytes, then `fs.unlinkSync`
3. **Overwrite `userData/security.json`** — 3 passes of random bytes, then `fs.unlinkSync`
4. **Overwrite `userData/settings.json`** — 3 passes of random bytes, then `fs.unlinkSync`
5. **Overwrite fingerprint temp files** — all `pane-fingerprints/fp-*.js` files, 3 passes then delete
6. **Clear in-memory state** — reset all Zustand stores to defaults
7. **Restart app** — `app.relaunch()` + `app.exit(0)` — app comes back clean, as if freshly installed

Each file gets 3 overwrite passes with `crypto.randomBytes` before deletion. Defeats casual file recovery tools.

## Settings UI

New "Security" section in the Settings page.

### No PIN Configured (`pin === null`)

- "Set up PIN" button
- Opens a sheet with:
  - "Enter new PIN" — reuses the numpad component from the lock screen
  - "Confirm PIN" — re-enter to verify match
  - Mismatch: shake animation + "PINs don't match" error, retry
  - On success: hash PIN with bcrypt, save to `securityStore`

### PIN Configured (`pin !== null`)

- "Change PIN" button — sheet requires current PIN, then new PIN + confirm
- "Remove PIN" button — sheet requires current PIN, then clears `pin` from store and sets `isLocked: false`

All PIN entry in settings reuses the same numpad component rendered inside a sheet.

## Boot Sequence

1. App launches, `index.html` splash screen shows (pulsing logo)
2. `app.whenReady()` rehydrates `securityStore` along with `profileStore` and `settingsStore`
3. If `securityStore.pin !== null`, set `isLocked: true`
4. React mounts, `Layout` checks `securityStore.isLocked`
5. `dismissSplash()` fires — fades out, revealing the lock screen underneath
6. User enters PIN → verified against hash in main process via tRPC `security.verify`
7. On success: `isLocked = false`, `failedAttempts = 0`, Layout renders sidebar + content panel

### No PIN Configured

Steps 3-6 skip entirely. Splash fades into normal app. Zero change to current behavior.

### Transition

When `isLocked` goes from `true` to `false`, the full-width content panel crossfades into the sidebar + content panel layout (~300ms opacity transition).

## Prerequisite Refactor: Next.js App Router Convention

Before adding the lock screen, refactor `renderer/pages/` to `renderer/app/` following Next.js App Router filesystem conventions:

- Rename `renderer/pages/` → `renderer/app/`
- `renderer/app/layout.tsx` — stays as a file (not in a subfolder)
- `renderer/app/surface.tsx` — stays as a file (not in a subfolder)
- `renderer/app/browser/index.tsx` → `renderer/app/browser/page.tsx`
- `renderer/app/settings/index.tsx` → `renderer/app/settings/page.tsx`
- New: `renderer/app/lock-screen/page.tsx`

Update all imports accordingly (`main.tsx`, `layout.tsx`).

## Architecture Notes

- **Approach:** Lock screen as a Layout-level gate. `Layout` checks `securityStore.isLocked` before rendering the page enum — if locked, renders full-width content panel with `LockScreen`. No new page enum value needed.
- **PIN verification:** tRPC call to main process (`security.verify`) — bcrypt compare happens in main, not renderer.
- **Anti-forensics wipe:** tRPC mutation in main process (`security.wipe`) — renderer triggers it, main executes file operations.
- **Reused component:** The numpad component is shared between the lock screen and settings PIN entry sheets.
