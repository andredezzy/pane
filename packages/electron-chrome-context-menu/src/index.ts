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
	} else if (params.mediaType === "video" || params.mediaType === "audio") {
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

			if (params.dictionarySuggestions.length > 0) {
				appendSeparator();
			}

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
				!["input-number", "input-telephone"].includes(params.formControlType)
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

		if (params.selectionText.trim()) {
			append({
				label: labels.searchGoogle(params.selectionText.trim()),
				click: () =>
					openLink(
						`https://www.google.com/search?q=${encodeURIComponent(params.selectionText.trim())}`,
						"default",
						params,
					),
			});
		}

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

		if (extensionMenuItems.length > 0) {
			appendSeparator();
		}
	}

	append({
		label: labels.inspect,
		click: () => {
			if (!webContents.isDevToolsOpened()) {
				webContents.openDevTools({ mode: "detach" });
			}

			webContents.inspectElement(params.x, params.y);

			if (!webContents.isDevToolsFocused()) {
				webContents.devToolsWebContents?.focus();
			}
		},
	});

	return menu;
}
