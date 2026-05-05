# Full-Screen PIN Screen

## Overview

Replace the sheet-based PIN setup/change/remove flows with a unified full-screen PIN screen. All PIN interactions share the same full-width ContentPanel layout used by the lock screen.

## Data Model Changes

Add `pinScreen` to `SecurityState`:

```typescript
type PinScreenMode = "SETUP" | "CHANGE" | "REMOVE";

interface SecurityState {
  pin: {
    hash: string;
    length: number;
  } | null;
  failedAttempts: number;
  isLocked: boolean;
  pinScreen: {
    mode: PinScreenMode | null;
    show: (mode: PinScreenMode) => void;
    dismiss: () => void;
  };

  setPin: (pin: Pin) => void;
  clearPin: () => void;
  lock: () => void;
  unlock: () => void;
  recordFailedAttempt: () => number;
  resetAttempts: () => void;
}
```

`pinScreen.mode` is `null` when no PIN interaction is active. `pinScreen.show(mode)` and `pinScreen.dismiss()` are actions grouped with their state.

## Layout Logic

```
if isLocked → PinScreen mode="UNLOCK"
else if pinScreen.mode !== null → PinScreen mode={pinScreen.mode}
else → normal sidebar + content
```

Both locked and settings-triggered PIN flows render identically: full-width ContentPanel, no sidebar, numpad centered.

## PinScreen Component

The existing `lock-screen/page.tsx` expands into a multi-mode PIN screen. It receives a `mode` prop and manages a step-based flow internally.

### Flows by mode

| Mode | Steps |
|------|-------|
| `UNLOCK` | Enter PIN → verify → fade out → unlock |
| `SETUP` | Enter new PIN → "Continue with N-digit PIN" → confirm → save → dismiss |
| `CHANGE` | Verify current PIN → enter new PIN → confirm → save → dismiss |
| `REMOVE` | Verify current PIN → remove → dismiss |

### Step state machine

```
UNLOCK:  ENTER → (verify) → done
SETUP:   ENTER → CONFIRM → (save) → done
CHANGE:  VERIFY → ENTER → CONFIRM → (save) → done
REMOVE:  VERIFY → (remove) → done
```

### UI per step

- **VERIFY step:** PIN dots matching `pin.length`, title "Enter current PIN"
- **ENTER step (setup/change):** Dots grow as user types (min 4 shown), title "Enter new PIN", "Continue with N-digit PIN" button appears at 4+ digits
- **ENTER step (unlock):** Fixed dots matching `pin.length`, no title text, attempts warning shown
- **CONFIRM step:** Fixed dots matching first PIN length, title "Confirm PIN"

Wrong PIN feedback: shake + clear after 600ms (same as current lock screen).

### Dismiss behavior

- `UNLOCK` mode: no dismiss — user must enter correct PIN or get wiped
- `SETUP` / `CHANGE` / `REMOVE` modes: Escape key or a back button dismisses, returning to settings

## Settings UI Changes

The settings Security section replaces `surface.open(...)` calls with direct store actions:

- "Set up PIN" → `securityStore.getState().pinScreen.show("SETUP")`
- "Change PIN" → `securityStore.getState().pinScreen.show("CHANGE")`
- "Remove PIN" → `securityStore.getState().pinScreen.show("REMOVE")`

No more surface/sheet involvement for PIN flows.

## Files

- **Delete:** `renderer/sheets/pin-setup.tsx`, `renderer/sheets/pin-verify.tsx`
- **Modify:** `stores/security-store.ts` — add `pinScreen` nested object
- **Modify:** `renderer/app/lock-screen/page.tsx` — expand to multi-mode PinScreen with step state machine
- **Modify:** `renderer/app/layout.tsx` — add `pinScreen.mode` gate
- **Modify:** `renderer/app/settings/page.tsx` — replace surface.open calls with store actions, remove sheet imports
