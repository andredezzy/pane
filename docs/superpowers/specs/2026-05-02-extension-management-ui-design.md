# Extension Management UI

## Summary

Add an extension management section to the Settings page that lets users install Chrome extensions from the Web Store (via URL or raw ID), view installed extensions with icon/name/version/description, and uninstall with confirmation.

## Decisions

- **Location:** Settings page, new section below existing Browser section
- **Install input:** Paste a CWS URL or raw extension ID — we parse the ID from the URL
- **List layout:** Card rows with icon (32px), name, version, description
- **Actions:** Uninstall with confirmation dialog
- **Install feedback:** Inline loading state on button + input disabled; error toasts via Sonner
- **Architecture:** Presentational primitives in `extension-settings.tsx`, orchestration logic inline in `settings-page.tsx`
- **UI library:** Use `@pane/ui` shadcn primitives; install additional shadcn components as needed

## File changes

### New files

**`apps/desktop/src/renderer/components/extension-settings.tsx`** — presentational primitives:
- `ExtensionInstallForm` — form with URL/ID input + install button. Props: `onInstall(value: string)`, `isInstalling`. Uses react-hook-form + zod for validation.
- `ExtensionList` — wrapper div for the list of extensions
- `ExtensionItem` — card row displaying a single extension. Props: `extension: InstalledExtension`, `onUninstall(id: string)`. Shows icon, name, version, description, uninstall button.
- `UninstallDialog` — confirmation dialog. Props: `extension: InstalledExtension | null`, `open: boolean`, `onOpenChange`, `onConfirm`. Uses `@pane/ui` Dialog.

### Modified files

**`apps/desktop/src/renderer/components/settings-page.tsx`** — add extensions section:
- `useEffect` on mount: call `window.pane.cws.installed()` → set `extensions` state
- `handleInstall(value: string)`: parse ID from URL/raw, call `window.pane.cws.install(id)`, on success add to local list from the returned Extension manifest data, on error `toast.error()`
- `handleUninstall(id: string)`: call `window.pane.cws.uninstall(id)`, remove from local list, on error `toast.error()`
- Render: separator, "Extensions" section label, `ExtensionInstallForm`, `ExtensionList` with `ExtensionItem` cards, `UninstallDialog`

**`apps/desktop/src/main/pane-extensions.ts`** — enrich `installed()` return type:
- Add `description: string` — read from `manifest.description`, resolve `__MSG_*__` i18n keys (same logic already used for `name`)
- Add `icon: string` — `pane-extension://<id>/icon` protocol URL pointing to the largest icon from `manifest.icons`

**`apps/desktop/src/main/index.ts`** — register custom protocol:
- Register `pane-extension://` protocol via `protocol.handle()` in `app.whenReady()`, before `createWindow()`
- Route: `pane-extension://<extensionId>/icon` → reads the largest icon file from `<extensionsPath>/<id>/<version>/<manifest.icons[largest]>`

## Data model

### `InstalledExtension` (enriched return from `cws:installed`)

Defined in `pane-extensions.ts` (main process, colocated with `installed()` which produces it). The renderer re-declares the same shape locally in `extension-settings.tsx` since Electron main/renderer can't share imports directly — the IPC boundary serializes to JSON anyway.

```ts
interface InstalledExtension {
  id: string;          // Chrome extension ID
  name: string;        // Resolved human-readable name (i18n)
  version: string;     // From manifest.version
  description: string; // From manifest.description (i18n resolved)
  icon: string;        // Protocol URL: pane-extension://<id>/icon
}
```

### Install form validation schema (zod)

```ts
const CWS_URL_PATTERN = /chromewebstore\.google\.com\/.*\/([a-z]{32})/;
const EXTENSION_ID_PATTERN = /^[a-z]{32}$/;

const installSchema = z.object({
  value: z.string().min(1).refine(
    (v) => EXTENSION_ID_PATTERN.test(v) || CWS_URL_PATTERN.test(v),
    "Enter a valid Chrome Web Store URL or extension ID"
  ),
});
```

### ID extraction

```ts
function parseExtensionId(input: string): string | null {
  const urlMatch = input.match(CWS_URL_PATTERN);
  if (urlMatch) return urlMatch[1];
  if (EXTENSION_ID_PATTERN.test(input)) return input;
  return null;
}
```

## UI behavior

### Install flow
1. User pastes URL or ID into input
2. Zod validates format on submit
3. `parseExtensionId()` extracts the 32-char ID
4. Button shows spinner, input is disabled
5. `window.pane.cws.install(id)` is called
6. On success: extension added to local `extensions` state from the returned manifest data, input clears
7. On error: `toast.error("Failed to install extension")`, loading stops

### Installed list
- Fetched on mount via `window.pane.cws.installed()`
- Each `ExtensionItem` shows: icon (32px, loaded via `pane-extension://` protocol), name, version (muted), description (muted, truncated single line), ghost uninstall button

### Uninstall flow
1. User clicks "Uninstall" on an item
2. `UninstallDialog` opens: "Uninstall {name}?" with "This will remove the extension from all profiles."
3. Cancel dismisses; Confirm calls `window.pane.cws.uninstall(id)`
4. On success: item removed from local state, dialog closes
5. On error: `toast.error()`, dialog stays open

### Empty state
- When no extensions installed: "No extensions installed" muted text below the install form

## Component props

```tsx
// ExtensionInstallForm
interface ExtensionInstallFormProps {
  onInstall: (value: string) => void;
  isInstalling: boolean;
}

// ExtensionItem
interface ExtensionItemProps {
  extension: InstalledExtension;
  onUninstall: (id: string) => void;
}

// UninstallDialog
interface UninstallDialogProps {
  extension: InstalledExtension | null;
  open: boolean;
  onOpenChange: (open: boolean) => void;
  onConfirm: () => void;
}
```

## UI components needed from `@pane/ui`

**Already available:** Button, Dialog (DialogContent, DialogHeader, DialogTitle, DialogDescription, DialogFooter), Form (FormField, FormItem, FormLabel, FormControl, FormMessage), Input, Separator

**Not needed:** No new shadcn components required — the existing library covers all primitives for this feature.

## Custom protocol registration

```ts
// In Pane constructor or app ready
protocol.handle("pane-extension", (request) => {
  const url = new URL(request.url);
  const extensionId = url.hostname;
  // Resolve icon path from manifest.icons (largest available)
  // Return net.fetch(`file://${iconPath}`)
});
```

This is a read-only protocol that only serves icon files from the extensions directory. No security concerns beyond path traversal — validate that the resolved path stays within `extensionsPath`.
