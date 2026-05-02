# Extension Management UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an extensions section to the Settings page where users can install Chrome extensions via CWS URL/ID, view installed extensions, and uninstall them with confirmation.

**Architecture:** Presentational primitives in `extension-settings.tsx`, orchestration logic inline in `settings-page.tsx`. Main process enriches `installed()` to return description + icon. A custom `pane-extension://` protocol serves extension icons to the renderer.

**Tech Stack:** React, Zustand, react-hook-form, Zod v4, @pane/ui (shadcn), Electron protocol.handle, Sonner toasts

**Spec:** `docs/superpowers/specs/2026-05-02-extension-management-ui-design.md`

---

### Task 1: Register `pane-extension://` custom protocol

**Files:**
- Modify: `apps/desktop/src/main/index.ts:78` (app.whenReady)
- Modify: `apps/desktop/src/main/pane-extensions.ts:12-17` (InstalledExtension interface)

This task registers the protocol that serves extension icon files to the renderer, and enriches the `InstalledExtension` type with `description` and `icon` fields.

- [ ] **Step 1: Add `description` and `icon` to InstalledExtension and update `installed()`**

In `apps/desktop/src/main/pane-extensions.ts`, update the interface and the `installed()` method:

```ts
export interface InstalledExtension {
  id: string;
  name: string;
  version: string;
  description: string;
  icon: string;
}
```

In the `installed()` method, after resolving `name`, add description resolution with the same i18n pattern, resolve the icon URL, and drop the `path` field from the return:

```ts
let description: string = manifest.description ?? "";
if (description.startsWith("__MSG_") && description.endsWith("__")) {
  const msgKey = description.slice(6, -2);
  try {
    const messagesPath = path.join(extDir, version, "_locales", "en", "messages.json");
    const messages = JSON.parse(fs.readFileSync(messagesPath, "utf-8"));
    description = messages[msgKey]?.message ?? description;
  } catch {}
}

const icons: Record<string, string> | undefined = manifest.icons;
let icon = "";
if (icons) {
  const largest = Object.keys(icons)
    .map(Number)
    .sort((a, b) => b - a)[0];
  if (largest) icon = `pane-extension://${extId}/icon`;
}

result.push({ id: extId, name, version: manifest.version ?? version, description, icon });
```

- [ ] **Step 2: Register the `pane-extension://` protocol**

In `apps/desktop/src/main/index.ts`, add the protocol registration inside `app.whenReady()`, before `createWindow()`:

```ts
import { app, BaseWindow, Menu, net, protocol, WebContentsView } from "electron";
```

Then inside `app.whenReady().then(() => { ... })`, before `createWindow()`:

```ts
const extensionsPath = path.join(app.getPath("userData"), "Extensions");

protocol.handle("pane-extension", (request) => {
  const url = new URL(request.url);
  const extensionId = url.hostname;

  const extDir = path.join(extensionsPath, extensionId);
  if (!fs.existsSync(extDir)) return new Response("Not found", { status: 404 });

  const versions = fs.readdirSync(extDir);
  if (versions.length === 0) return new Response("Not found", { status: 404 });

  const versionDir = path.join(extDir, versions[0]);
  const manifestPath = path.join(versionDir, "manifest.json");
  if (!fs.existsSync(manifestPath)) return new Response("Not found", { status: 404 });

  const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf-8"));
  const icons: Record<string, string> | undefined = manifest.icons;
  if (!icons) return new Response("Not found", { status: 404 });

  const largest = Object.keys(icons).map(Number).sort((a, b) => b - a)[0];
  if (!largest || !icons[String(largest)]) return new Response("Not found", { status: 404 });

  const iconPath = path.join(versionDir, icons[String(largest)]);
  const resolved = path.resolve(iconPath);
  if (!resolved.startsWith(path.resolve(extensionsPath))) {
    return new Response("Forbidden", { status: 403 });
  }

  return net.fetch(`file://${resolved}`);
});
```

Add `import fs from "node:fs";` at the top of the file.

- [ ] **Step 3: Verify the build compiles**

Run: `cd /Users/andrevictor/www/pane/apps/desktop && npx electron-vite build`
Expected: Build succeeds with no errors.

- [ ] **Step 4: Commit**

```bash
git add apps/desktop/src/main/index.ts apps/desktop/src/main/pane-extensions.ts
git commit -m "feat: add pane-extension:// protocol and enrich InstalledExtension with description + icon"
```

---

### Task 2: Create presentational primitives (`extension-settings.tsx`)

**Files:**
- Create: `apps/desktop/src/renderer/components/extension-settings.tsx`

This file contains all the presentational components: `ExtensionInstallForm`, `ExtensionList`, `ExtensionItem`, and `UninstallDialog`. No IPC calls, no state management — just props in, UI out.

- [ ] **Step 1: Create the file with the InstalledExtension type and ID parsing utility**

Create `apps/desktop/src/renderer/components/extension-settings.tsx`:

```tsx
import { zodResolver } from "@hookform/resolvers/zod";
import { Button } from "@pane/ui/components/button";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@pane/ui/components/dialog";
import {
  Form,
  FormControl,
  FormField,
  FormItem,
  FormLabel,
  FormMessage,
} from "@pane/ui/components/form";
import { Input } from "@pane/ui/components/input";
import { Loader2 } from "lucide-react";
import { useForm } from "react-hook-form";
import { z } from "zod/v4";

export interface InstalledExtension {
  id: string;
  name: string;
  version: string;
  description: string;
  icon: string;
}

const CWS_URL_PATTERN = /chromewebstore\.google\.com\/.*\/([a-z]{32})/;
const EXTENSION_ID_PATTERN = /^[a-z]{32}$/;

export function parseExtensionId(input: string): string | null {
  const urlMatch = input.match(CWS_URL_PATTERN);
  if (urlMatch) return urlMatch[1];
  if (EXTENSION_ID_PATTERN.test(input)) return input;
  return null;
}

const installSchema = z.object({
  value: z
    .string()
    .min(1, "Enter a Chrome Web Store URL or extension ID")
    .refine(
      (v) => EXTENSION_ID_PATTERN.test(v) || CWS_URL_PATTERN.test(v),
      "Enter a valid Chrome Web Store URL or extension ID",
    ),
});

type InstallValues = z.infer<typeof installSchema>;
```

- [ ] **Step 2: Add `ExtensionInstallForm`**

Append to the same file:

```tsx
interface ExtensionInstallFormProps {
  onInstall: (value: string) => void;
  isInstalling: boolean;
}

export function ExtensionInstallForm({
  onInstall,
  isInstalling,
}: ExtensionInstallFormProps) {
  const form = useForm<InstallValues>({
    resolver: zodResolver(installSchema),
    defaultValues: { value: "" },
  });

  const onSubmit = (data: InstallValues) => {
    onInstall(data.value);
    form.reset();
  };

  return (
    <Form {...form}>
      <form onSubmit={form.handleSubmit(onSubmit)} className="mt-3">
        <FormField
          control={form.control}
          name="value"
          render={({ field }) => (
            <FormItem>
              <FormLabel className="text-[11px]">
                Install from Chrome Web Store
              </FormLabel>
              <div className="flex gap-2">
                <FormControl>
                  <Input
                    placeholder="Paste extension URL or ID"
                    disabled={isInstalling}
                    {...field}
                  />
                </FormControl>
                <Button
                  type="submit"
                  className="shrink-0"
                  disabled={isInstalling}
                >
                  {isInstalling ? (
                    <Loader2 className="h-4 w-4 animate-spin" />
                  ) : (
                    "Install"
                  )}
                </Button>
              </div>
              <FormMessage />
            </FormItem>
          )}
        />
      </form>
    </Form>
  );
}
```

- [ ] **Step 3: Add `ExtensionList` and `ExtensionItem`**

Append to the same file:

```tsx
interface ExtensionListProps {
  children: React.ReactNode;
}

export function ExtensionList({ children }: ExtensionListProps) {
  return <div className="mt-4 flex flex-col gap-1.5">{children}</div>;
}

interface ExtensionItemProps {
  extension: InstalledExtension;
  onUninstall: (id: string) => void;
}

export function ExtensionItem({ extension, onUninstall }: ExtensionItemProps) {
  return (
    <div className="flex items-start gap-3 rounded-lg border border-border/50 bg-card/30 p-3">
      {extension.icon ? (
        <img
          src={extension.icon}
          alt=""
          className="h-8 w-8 shrink-0 rounded-md"
        />
      ) : (
        <div className="h-8 w-8 shrink-0 rounded-md bg-muted" />
      )}
      <div className="min-w-0 flex-1">
        <div className="flex items-baseline gap-1.5">
          <span className="text-[13px] font-medium text-accent-foreground">
            {extension.name}
          </span>
          <span className="text-[11px] text-muted-foreground">
            v{extension.version}
          </span>
        </div>
        {extension.description && (
          <p className="mt-0.5 truncate text-[12px] text-muted-foreground">
            {extension.description}
          </p>
        )}
      </div>
      <Button
        variant="ghost"
        size="sm"
        className="shrink-0 text-destructive hover:text-destructive"
        onClick={() => onUninstall(extension.id)}
      >
        Uninstall
      </Button>
    </div>
  );
}
```

- [ ] **Step 4: Add `UninstallDialog`**

Append to the same file:

```tsx
interface UninstallDialogProps {
  extension: InstalledExtension | null;
  open: boolean;
  onOpenChange: (open: boolean) => void;
  onConfirm: () => void;
}

export function UninstallDialog({
  extension,
  open,
  onOpenChange,
  onConfirm,
}: UninstallDialogProps) {
  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-w-sm">
        <DialogHeader>
          <DialogTitle>Uninstall {extension?.name}?</DialogTitle>
          <DialogDescription>
            This will remove the extension from all profiles.
          </DialogDescription>
        </DialogHeader>
        <DialogFooter>
          <Button variant="outline" onClick={() => onOpenChange(false)}>
            Cancel
          </Button>
          <Button variant="destructive" onClick={onConfirm}>
            Uninstall
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
```

- [ ] **Step 5: Verify the build compiles**

Run: `cd /Users/andrevictor/www/pane/apps/desktop && npx electron-vite build`
Expected: Build succeeds with no errors.

- [ ] **Step 6: Commit**

```bash
git add apps/desktop/src/renderer/components/extension-settings.tsx
git commit -m "feat: add extension settings presentational components"
```

---

### Task 3: Integrate extension management into the Settings page

**Files:**
- Modify: `apps/desktop/src/renderer/components/settings-page.tsx`

Wire the presentational primitives into the existing SettingsPage with IPC calls, local state, and error handling.

- [ ] **Step 1: Add imports and state to SettingsPage**

In `apps/desktop/src/renderer/components/settings-page.tsx`, add these imports:

```tsx
import { useCallback, useEffect, useState } from "react";
```

(Replace the existing `import { useEffect } from "react";`)

And add the extension component imports:

```tsx
import {
  ExtensionInstallForm,
  ExtensionItem,
  ExtensionList,
  type InstalledExtension,
  parseExtensionId,
  UninstallDialog,
} from "./extension-settings";
```

- [ ] **Step 2: Add extension state and handlers inside the `SettingsPage` component**

Inside the `SettingsPage` function body, after the existing `onSubmit` function, add:

```tsx
const [extensions, setExtensions] = useState<InstalledExtension[]>([]);
const [isInstalling, setIsInstalling] = useState(false);
const [uninstallTarget, setUninstallTarget] =
  useState<InstalledExtension | null>(null);

useEffect(() => {
  window.pane.cws.installed().then(setExtensions);
}, []);

const handleInstall = useCallback(async (value: string) => {
  const id = parseExtensionId(value);
  if (!id) return;

  setIsInstalling(true);
  try {
    const ext = await window.pane.cws.install(id);
    if (ext) {
      const updated = await window.pane.cws.installed();
      setExtensions(updated);
    } else {
      toast.error("Failed to install extension");
    }
  } catch {
    toast.error("Failed to install extension");
  } finally {
    setIsInstalling(false);
  }
}, []);

const handleUninstall = useCallback(async () => {
  if (!uninstallTarget) return;
  try {
    await window.pane.cws.uninstall(uninstallTarget.id);
    setExtensions((prev) =>
      prev.filter((e) => e.id !== uninstallTarget.id),
    );
    setUninstallTarget(null);
  } catch {
    toast.error("Failed to uninstall extension");
  }
}, [uninstallTarget]);
```

- [ ] **Step 3: Add the extensions section to the JSX**

In the return statement, after the closing `</div>` of the Browser section (right before the final two closing `</div>`s), add:

```tsx
<div className="h-px bg-[rgba(255,255,255,0.05)]" />

<div>
  <span className="font-medium text-[10px] text-muted-foreground uppercase tracking-wide">
    Extensions
  </span>

  <ExtensionInstallForm
    onInstall={handleInstall}
    isInstalling={isInstalling}
  />

  {extensions.length > 0 ? (
    <ExtensionList>
      {extensions.map((ext) => (
        <ExtensionItem
          key={ext.id}
          extension={ext}
          onUninstall={() => setUninstallTarget(ext)}
        />
      ))}
    </ExtensionList>
  ) : (
    <p className="mt-4 text-[12px] text-muted-foreground">
      No extensions installed
    </p>
  )}
</div>

<UninstallDialog
  extension={uninstallTarget}
  open={uninstallTarget !== null}
  onOpenChange={(open) => {
    if (!open) setUninstallTarget(null);
  }}
  onConfirm={handleUninstall}
/>
```

- [ ] **Step 4: Verify the build compiles**

Run: `cd /Users/andrevictor/www/pane/apps/desktop && npx electron-vite build`
Expected: Build succeeds with no errors.

- [ ] **Step 5: Commit**

```bash
git add apps/desktop/src/renderer/components/settings-page.tsx
git commit -m "feat: integrate extension management into settings page"
```

---

### Task 4: Smoke test the full flow

**Files:** None — manual testing only.

- [ ] **Step 1: Clean test environment**

```bash
rm -rf "$HOME/Library/Application Support/@pane"
```

- [ ] **Step 2: Launch the app**

```bash
cd /Users/andrevictor/www/pane/apps/desktop && npx electron-vite dev
```

- [ ] **Step 3: Test the install flow**

1. Click the Settings gear in the sidebar footer
2. Scroll down to the "Extensions" section
3. Verify the install form shows with placeholder "Paste extension URL or ID"
4. Verify "No extensions installed" text appears below
5. Paste `https://chromewebstore.google.com/detail/dark-reader/eimadpbcbfnmbkopoojfekhnkhdbieeh` into the input
6. Click "Install" — verify spinner appears, input is disabled
7. After install completes, verify Dark Reader appears in the list with icon, name, version, and description

- [ ] **Step 4: Test the uninstall flow**

1. Click "Uninstall" on the Dark Reader item
2. Verify confirmation dialog appears: "Uninstall Dark Reader?" with description "This will remove the extension from all profiles."
3. Click "Cancel" — verify dialog closes, extension still listed
4. Click "Uninstall" again, then click "Uninstall" in the dialog
5. Verify extension disappears from the list
6. Verify "No extensions installed" text reappears

- [ ] **Step 5: Test validation**

1. Submit the form empty — verify error message appears
2. Type "invalid-text" — verify error message about valid URL or ID
3. Paste a valid raw ID: `eimadpbcbfnmbkopoojfekhnkhdbieeh` — verify it installs successfully

- [ ] **Step 6: Test icon loading**

1. With an extension installed, verify the icon loads from `pane-extension://` protocol
2. Open DevTools (Cmd+Option+I), check the Network tab for `pane-extension://` requests — verify 200 responses

- [ ] **Step 7: Commit any fixes**

If any bugs were found and fixed during testing, commit the fixes:

```bash
git add -p
git commit -m "fix: address issues found during extension UI smoke test"
```
