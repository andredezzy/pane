# Right-Click Context Menu for Browser Tabs

## Problem

Electron's `WebContentsView` does not show context menus automatically. Right-clicking inside a browser tab in Pane does nothing — no copy, no open link, no inspect element. Users expect full Chrome-like context menu behavior.

## Solution

Fork `electron-chrome-context-menu` (~150 lines, MIT) from `samuelmaddock/electron-browser-shell` into `packages/electron-chrome-context-menu` as `@pane/electron-chrome-context-menu`. Refactor, extend with missing Chrome items, and integrate into `profile-tabs.ts`.

## Package: `@pane/electron-chrome-context-menu`

### Location

`packages/electron-chrome-context-menu/`

### Exports

Single named export:

```ts
export function buildChromeContextMenu(options: ChromeContextMenuOptions): Menu;
```

### Options

```ts
interface ChromeContextMenuOptions {
  params: Electron.ContextMenuParams;
  webContents: Electron.WebContents;
  openLink: (
    url: string,
    disposition: "default" | "foreground-tab" | "background-tab" | "new-window",
    params: Electron.ContextMenuParams,
  ) => void;
  extensionMenuItems?: Electron.MenuItem[];
  labels?: ChromeContextMenuLabels;
}
```

### Menu Items by Context

**Link (`params.linkURL` is truthy):**
- Open link in new tab
- separator
- Save link as...
- Copy link address
- separator

**Image (`params.mediaType === "image"`):**
- Open image in new tab
- Save image as...
- Copy image
- Copy image address
- separator

**Link + Image (both present):** Link section first, then image section. The original uses `else if` which prevents showing both — this is fixed.

**Video/Audio (`params.mediaType === "video" | "audio"`):**
- Open video/audio in new tab
- Save video/audio as...
- Copy video/audio address
- separator

**Editable field (`params.isEditable`):**
- Spell suggestions (if `params.misspelledWord`) + Add to dictionary
- OR: Emoji (if supported), Undo, Redo
- separator
- Cut, Copy, Paste, Delete
- separator
- Select All
- separator

**Text selection (not editable, `params.selectionText`):**
- Copy
- Search Google for "..."
- separator

**Page background (no link, no media, no selection, not editable):**
- Back (enabled: `navigationHistory.canGoBack()`)
- Forward (enabled: `navigationHistory.canGoForward()`)
- Reload
- separator

**Always appended:**
- Extension menu items (from `getContextMenuItems`)
- separator (if extension items present)
- Inspect

### Electron APIs Used

| Action | API |
|---|---|
| Copy text | `clipboard.writeText()` |
| Copy image | `webContents.copyImageAt(x, y)` |
| Save image/link/video | `webContents.downloadURL(url)` |
| Edit operations | `webContents.cut/copy/paste/delete/undo/redo/selectAll()` |
| Spell replace | `webContents.replaceMisspelling()` |
| Add to dictionary | `session.addWordToSpellCheckerDictionary()` |
| Navigate back/forward | `webContents.navigationHistory.goBack/goForward()` |
| Inspect element | `webContents.inspectElement(x, y)` + `devToolsWebContents?.focus()` |
| Emoji panel | `app.showEmojiPanel()` |
| Search Google | `openLink` callback with `https://www.google.com/search?q=...` |

### Refactoring from Original

1. Remove `getBrowserWindowFromWebContents` — uses deprecated `getBrowserViews()`. Fullscreen detection not needed.
2. Remove deprecated `strings` option — clean API only.
3. Fix Undo/Redo order — original has Redo before Undo. Chrome shows Undo first.
4. Change link/image branching from `else if` to sequential `if` — both sections show when right-clicking a linked image.
5. Biome formatting — align with monorepo linter/formatter.

### Build Setup

- esbuild config matching `@pane/electron-chrome-extensions` pattern
- Dual CJS + ESM output
- TypeScript declarations via `tsc --emitDeclarationOnly`
- Peer dependency: `electron >= 35.0.0`
- Zero runtime dependencies

## Integration in `profile-tabs.ts`

Add a `context-menu` event listener in `createView()`:

```ts
view.webContents.on("context-menu", (_event, params) => {
  const menu = buildChromeContextMenu({
    params,
    webContents: view.webContents,
    openLink: (url) => this.open(url),
    extensionMenuItems: this.profile.ece.getContextMenuItems(
      view.webContents,
      params,
    ),
  });

  menu.popup();
});
```

This follows the same pattern as the existing `found-in-page` listener. The `openLink` callback delegates to `this.open(url)` which creates a new tab with the given URL.

## Files Changed

### New files
- `packages/electron-chrome-context-menu/package.json`
- `packages/electron-chrome-context-menu/tsconfig.json`
- `packages/electron-chrome-context-menu/esbuild.config.cjs`
- `packages/electron-chrome-context-menu/src/index.ts`

### Modified files
- `apps/veil/package.json` — add `@pane/electron-chrome-context-menu` dependency
- `apps/veil/src/main/profile/profile-tabs.ts` — add `context-menu` listener in `createView()`
