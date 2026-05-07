# Context Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Chrome-like right-click context menus to browser tab content by forking `electron-chrome-context-menu` into `@pane/electron-chrome-context-menu` and integrating it into `profile-tabs.ts`.

**Architecture:** Fork the ~150-line MIT-licensed `buildChromeContextMenu` from `samuelmaddock/electron-browser-shell`, refactor it (fix ordering, remove deprecated APIs, extend with missing Chrome items), package it as `@pane/electron-chrome-context-menu` with esbuild dual CJS/ESM output, then add a single `context-menu` event listener in `profile-tabs.ts` that delegates to the builder.

**Tech Stack:** Electron 41, esbuild, TypeScript

---

## File Map

### New files
| File | Responsibility |
|---|---|
| `packages/electron-chrome-context-menu/package.json` | Package manifest with peer dep on electron >= 35 |
| `packages/electron-chrome-context-menu/tsconfig.json` | TypeScript config for declaration emit |
| `packages/electron-chrome-context-menu/esbuild.config.cjs` | Build script: CJS + ESM dual output |
| `packages/electron-chrome-context-menu/src/index.ts` | `buildChromeContextMenu` function + types |

### Modified files
| File | Change |
|---|---|
| `apps/veil/package.json` | Add `@pane/electron-chrome-context-menu: workspace:*` dependency |
| `apps/veil/src/main/profile/profile-tabs.ts` | Add `context-menu` listener in `createView()`, add `import` |

---

### Task 1: Create `@pane/electron-chrome-context-menu` package scaffold

**Files:**
- Create: `packages/electron-chrome-context-menu/package.json`
- Create: `packages/electron-chrome-context-menu/tsconfig.json`
- Create: `packages/electron-chrome-context-menu/esbuild.config.cjs`

- [ ] **Step 1: Create `package.json`**

```json
{
  "name": "@pane/electron-chrome-context-menu",
  "version": "1.0.0",
  "private": true,
  "description": "Chrome-like context menu builder for Electron browser tabs (Pane fork)",
  "main": "./dist/cjs/index.js",
  "module": "./dist/esm/index.mjs",
  "types": "./dist/types/index.d.ts",
  "exports": {
    ".": {
      "types": "./dist/types/index.d.ts",
      "import": "./dist/esm/index.mjs",
      "require": "./dist/cjs/index.js"
    }
  },
  "scripts": {
    "build": "node esbuild.config.cjs && tsc"
  },
  "peerDependencies": {
    "electron": ">=35.0.0"
  },
  "devDependencies": {
    "esbuild": "^0.24.2"
  }
}
```

- [ ] **Step 2: Create `tsconfig.json`**

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "CommonJS",
    "moduleResolution": "node",
    "declaration": true,
    "declarationDir": "dist/types",
    "emitDeclarationOnly": true,
    "outDir": "dist/types",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true
  },
  "include": ["src/**/*"],
  "exclude": ["node_modules"]
}
```

- [ ] **Step 3: Create `esbuild.config.cjs`**

```js
const esbuild = require("esbuild");

const external = ["electron"];

const configs = [
  {
    entryPoints: ["src/index.ts"],
    outfile: "dist/cjs/index.js",
    bundle: true,
    platform: "node",
    format: "cjs",
    external,
  },
  {
    entryPoints: ["src/index.ts"],
    outfile: "dist/esm/index.mjs",
    bundle: true,
    platform: "node",
    format: "esm",
    external,
  },
];

Promise.all(configs.map((c) => esbuild.build(c)))
  .then(() => console.log("electron-chrome-context-menu built successfully"))
  .catch((e) => {
    console.error(e);
    process.exit(1);
  });
```

- [ ] **Step 4: Commit scaffold**

```bash
git add packages/electron-chrome-context-menu/package.json packages/electron-chrome-context-menu/tsconfig.json packages/electron-chrome-context-menu/esbuild.config.cjs
git commit -m "chore: scaffold @pane/electron-chrome-context-menu package"
```

---

### Task 2: Implement `buildChromeContextMenu`

**Files:**
- Create: `packages/electron-chrome-context-menu/src/index.ts`

This is the core implementation — forked from `samuelmaddock/electron-browser-shell` (MIT), refactored and extended.

- [ ] **Step 1: Create `src/index.ts`**

Key changes from the original:
- Remove `getBrowserWindowFromWebContents` helper and fullscreen detection (uses deprecated `getBrowserViews`)
- Remove deprecated `strings` option
- Fix Undo/Redo order (original has Redo before Undo — Chrome shows Undo first)
- Change link/image from `else if` to sequential `if` (both sections show for linked images)
- Add "Save image as..." via `webContents.downloadURL(srcURL)`
- Add "Copy image" via `webContents.copyImageAt(x, y)`
- Add "Save link as..." via `webContents.downloadURL(linkURL)`
- Add "Save video/audio as..." via `webContents.downloadURL(srcURL)`
- Add "Search Google for '...'" via `openLink` with Google search URL
- Biome-compatible formatting

```ts
import { app, clipboard, Menu, MenuItem } from "electron";

const LABELS = {
	openInNewTab: (type: "link" | Electron.ContextMenuParams["mediaType"]) =>
		`Open ${type} in new tab`,
	copyAddress: (type: "link" | Electron.ContextMenuParams["mediaType"]) =>
		`Copy ${type} address`,
	saveLinkAs: "Save link as...",
	saveImageAs: "Save image as...",
	copyImage: "Copy image",
	saveMediaAs: (type: "video" | "audio") => `Save ${type} as...`,
	searchGoogle: (text: string) => {
		const trimmed = text.length > 30 ? `${text.substring(0, 30)}...` : text;
		return `Search Google for "${trimmed}"`;
	},
	undo: "Undo",
	redo: "Redo",
	cut: "Cut",
	copy: "Copy",
	delete: "Delete",
	paste: "Paste",
	selectAll: "Select all",
	back: "Back",
	forward: "Forward",
	reload: "Reload",
	inspect: "Inspect",
	addToDictionary: "Add to dictionary",
	emoji: "Emoji",
};

export type ChromeContextMenuLabels = typeof LABELS;

export interface ChromeContextMenuOptions {
	params: Electron.ContextMenuParams;
	webContents: Electron.WebContents;
	openLink: (
		url: string,
		disposition: "default" | "foreground-tab" | "background-tab" | "new-window",
		params: Electron.ContextMenuParams,
	) => void;
	extensionMenuItems?: MenuItem[];
	labels?: ChromeContextMenuLabels;
}

export function buildChromeContextMenu(
	options: ChromeContextMenuOptions,
): Menu {
	const { params, webContents, openLink, extensionMenuItems } = options;
	const labels = options.labels ?? LABELS;

	const menu = new Menu();

	const append = (opts: Electron.MenuItemConstructorOptions) =>
		menu.append(new MenuItem(opts));

	const appendSeparator = () =>
		menu.append(new MenuItem({ type: "separator" }));

	if (params.linkURL) {
		append({
			label: labels.openInNewTab("link"),
			click: () => openLink(params.linkURL, "default", params),
		});

		appendSeparator();

		append({
			label: labels.saveLinkAs,
			click: () => webContents.downloadURL(params.linkURL),
		});

		append({
			label: labels.copyAddress("link"),
			click: () => clipboard.writeText(params.linkURL),
		});

		appendSeparator();
	}

	if (params.mediaType === "image") {
		append({
			label: labels.openInNewTab("image"),
			click: () => openLink(params.srcURL, "default", params),
		});

		append({
			label: labels.saveImageAs,
			click: () => webContents.downloadURL(params.srcURL),
		});

		append({
			label: labels.copyImage,
			click: () => webContents.copyImageAt(params.x, params.y),
		});

		append({
			label: labels.copyAddress("image"),
			click: () => clipboard.writeText(params.srcURL),
		});

		appendSeparator();
	} else if (
		params.mediaType === "video" ||
		params.mediaType === "audio"
	) {
		append({
			label: labels.openInNewTab(params.mediaType),
			click: () => openLink(params.srcURL, "default", params),
		});

		append({
			label: labels.saveMediaAs(params.mediaType),
			click: () => webContents.downloadURL(params.srcURL),
		});

		append({
			label: labels.copyAddress(params.mediaType),
			click: () => clipboard.writeText(params.srcURL),
		});

		appendSeparator();
	}

	if (params.isEditable) {
		if (params.misspelledWord) {
			for (const suggestion of params.dictionarySuggestions) {
				append({
					label: suggestion,
					click: () => webContents.replaceMisspelling(suggestion),
				});
			}

			if (params.dictionarySuggestions.length > 0) appendSeparator();

			append({
				label: labels.addToDictionary,
				click: () =>
					webContents.session.addWordToSpellCheckerDictionary(
						params.misspelledWord,
					),
			});
		} else {
			if (
				app.isEmojiPanelSupported() &&
				!["input-number", "input-telephone"].includes(
					params.formControlType,
				)
			) {
				append({
					label: labels.emoji,
					click: () => app.showEmojiPanel(),
				});

				appendSeparator();
			}

			append({
				label: labels.undo,
				enabled: params.editFlags.canUndo,
				click: () => webContents.undo(),
			});

			append({
				label: labels.redo,
				enabled: params.editFlags.canRedo,
				click: () => webContents.redo(),
			});
		}

		appendSeparator();

		append({
			label: labels.cut,
			enabled: params.editFlags.canCut,
			click: () => webContents.cut(),
		});

		append({
			label: labels.copy,
			enabled: params.editFlags.canCopy,
			click: () => webContents.copy(),
		});

		append({
			label: labels.paste,
			enabled: params.editFlags.canPaste,
			click: () => webContents.paste(),
		});

		append({
			label: labels.delete,
			enabled: params.editFlags.canDelete,
			click: () => webContents.delete(),
		});

		appendSeparator();

		if (params.editFlags.canSelectAll) {
			append({
				label: labels.selectAll,
				click: () => webContents.selectAll(),
			});

			appendSeparator();
		}
	} else if (params.selectionText) {
		append({
			label: labels.copy,
			click: () => clipboard.writeText(params.selectionText),
		});

		append({
			label: labels.searchGoogle(params.selectionText),
			click: () =>
				openLink(
					`https://www.google.com/search?q=${encodeURIComponent(params.selectionText)}`,
					"default",
					params,
				),
		});

		appendSeparator();
	}

	if (menu.items.length === 0) {
		append({
			label: labels.back,
			enabled: webContents.navigationHistory.canGoBack(),
			click: () => webContents.navigationHistory.goBack(),
		});

		append({
			label: labels.forward,
			enabled: webContents.navigationHistory.canGoForward(),
			click: () => webContents.navigationHistory.goForward(),
		});

		append({
			label: labels.reload,
			click: () => webContents.reload(),
		});

		appendSeparator();
	}

	if (extensionMenuItems) {
		for (const item of extensionMenuItems) {
			menu.append(item);
		}

		if (extensionMenuItems.length > 0) appendSeparator();
	}

	append({
		label: labels.inspect,
		click: () => {
			webContents.inspectElement(params.x, params.y);

			if (!webContents.isDevToolsFocused()) {
				webContents.devToolsWebContents?.focus();
			}
		},
	});

	return menu;
}
```

- [ ] **Step 2: Build the package**

```bash
cd packages/electron-chrome-context-menu
bun install
node esbuild.config.cjs
npx tsc
```

Expected: `dist/cjs/index.js`, `dist/esm/index.mjs`, `dist/types/index.d.ts` all created.

- [ ] **Step 3: Verify build output**

```bash
ls packages/electron-chrome-context-menu/dist/cjs/index.js
ls packages/electron-chrome-context-menu/dist/esm/index.mjs
ls packages/electron-chrome-context-menu/dist/types/index.d.ts
```

Expected: all three files exist.

- [ ] **Step 4: Commit**

```bash
git add packages/electron-chrome-context-menu/src/index.ts
git commit -m "feat: implement buildChromeContextMenu with Chrome-parity items"
```

---

### Task 3: Integrate into `profile-tabs.ts`

**Files:**
- Modify: `apps/veil/package.json` — add dependency
- Modify: `apps/veil/src/main/profile/profile-tabs.ts` — add context-menu listener

- [ ] **Step 1: Add workspace dependency to `apps/veil/package.json`**

Add to `dependencies`:
```json
"@pane/electron-chrome-context-menu": "workspace:*"
```

- [ ] **Step 2: Install dependencies**

```bash
bun install
```

- [ ] **Step 3: Add import to `profile-tabs.ts`**

Add at the top of `apps/veil/src/main/profile/profile-tabs.ts`:
```ts
import { buildChromeContextMenu } from "@pane/electron-chrome-context-menu";
```

- [ ] **Step 4: Add `context-menu` listener in `createView()`**

In the `createView()` method of `profile-tabs.ts`, after the existing `view.webContents.on("found-in-page", ...)` listener (around line 405), add:

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

- [ ] **Step 5: Build and verify**

```bash
turbo run build --filter=@pane/electron-chrome-context-menu
turbo run build --filter=@pane/veil
```

Expected: both build clean with no errors.

- [ ] **Step 6: Run the app and test context menus**

```bash
turbo run dev --filter=@pane/veil
```

Test these scenarios in a browser tab:
1. Right-click on empty page background — should show Back, Forward, Reload, Inspect
2. Right-click on a link — should show Open in new tab, Save link as, Copy link address, Inspect
3. Right-click on an image — should show Open image in new tab, Save image as, Copy image, Copy image address, Inspect
4. Right-click on selected text — should show Copy, Search Google for "...", Inspect
5. Right-click in an input field — should show Undo, Redo, Cut, Copy, Paste, Delete, Select all, Inspect
6. Right-click on a linked image — should show BOTH link items AND image items

- [ ] **Step 7: Lint check**

```bash
bun run lint
```

Expected: zero errors, zero warnings.

- [ ] **Step 8: Commit**

```bash
git add apps/veil/package.json apps/veil/src/main/profile/profile-tabs.ts bun.lock
git commit -m "feat: add right-click context menu to browser tabs"
```
