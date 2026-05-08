import path from "node:path";
import {
	app,
	BaseWindow,
	BrowserWindow,
	Menu,
	nativeImage,
	WebContentsView,
} from "electron";

import type { HotkeyEmitter } from "./emitters/hotkey-emitter";
import { HotkeyEvent } from "./emitters/hotkey-emitter";
import type { Pane } from "./pane";

export interface AppWindow {
	mainWindow: BaseWindow;
	chrome: WebContentsView;
	surface: BrowserWindow;
}

export function createAppWindow(): AppWindow {
	const resourcesDir = path.join(app.getAppPath(), "resources");
	const icon = nativeImage.createFromPath(path.join(resourcesDir, "icon.png"));

	if (process.platform === "darwin") {
		const dockIcon = nativeImage.createFromPath(
			path.join(resourcesDir, "icon-dock.png"),
		);

		if (!dockIcon.isEmpty()) {
			app.dock?.setIcon(dockIcon);
		}
	}

	const mainWindow = new BaseWindow({
		width: 1280,
		height: 800,
		minWidth: 900,
		minHeight: 600,
		title: "Pane",
		titleBarStyle: "hidden",
		trafficLightPosition: { x: 12, y: 12 },
		transparent: true,
		hasShadow: true,
		show: false,
		icon,
	});

	const webPreferences: Electron.WebPreferences = {
		preload: path.join(__dirname, "../preload/index.js"),
		contextIsolation: true,
		sandbox: true,
	};

	const loadRenderer = (
		webContents: Electron.WebContents,
		params?: Record<string, string>,
	) => {
		if (process.env.ELECTRON_RENDERER_URL) {
			const url = new URL(process.env.ELECTRON_RENDERER_URL);

			if (params) {
				for (const [key, value] of Object.entries(params)) {
					url.searchParams.set(key, value);
				}
			}

			webContents.loadURL(url.toString());
		} else {
			webContents.loadFile(path.join(__dirname, "../renderer/index.html"), {
				query: params,
			});
		}
	};

	const chrome = new WebContentsView({ webPreferences });

	chrome.setBackgroundColor("#0a0a0c");
	mainWindow.contentView.addChildView(chrome);

	const [width, height] = mainWindow.getContentSize();
	chrome.setBounds({ x: 0, y: 0, width, height });

	loadRenderer(chrome.webContents);

	chrome.webContents.once("did-finish-load", () => {
		mainWindow.show();
		chrome.webContents.focus();
	});

	const bounds = mainWindow.getBounds();

	const surface = new BrowserWindow({
		parent: mainWindow,
		frame: false,
		transparent: true,
		hasShadow: false,
		show: false,
		x: bounds.x,
		y: bounds.y,
		width,
		height,
		backgroundColor: "#00000000",
		webPreferences,
	});

	loadRenderer(surface.webContents, { surface: "true" });

	return { mainWindow, chrome, surface };
}

function openTabSwitcher(
	surface: BrowserWindow,
	hotkeyEmitter: HotkeyEmitter,
	direction: HotkeyEvent,
): void {
	if (!surface.isVisible()) {
		surface.show();

		surface.webContents.executeJavaScript(
			`window.postMessage(${JSON.stringify({ name: "TabSwitcher" })})`,
		);
	}

	hotkeyEmitter.emitHotkey(direction);
}

export function createMenu(
	chrome: WebContentsView,
	pane: Pane,
	hotkeyEmitter: HotkeyEmitter,
	surface: BrowserWindow,
): void {
	const menu = Menu.buildFromTemplate([
		{ role: "appMenu" },
		{
			label: "File",
			submenu: [
				{
					label: "New tab",
					accelerator: "CommandOrControl+T",
					click: () => {
						const { activeProfileId } = pane.tabStore.getState();

						if (activeProfileId) {
							pane.hideAllTabs();
							pane.getOrCreateProfile(activeProfileId).tabs.open(null);
							pane.navigateToBrowser();
							chrome.webContents.focus();
							setTimeout(() => hotkeyEmitter.emitHotkey(HotkeyEvent.FOCUS_ADDRESS_BAR), 100);
						}
					},
				},
				{
					label: "Close tab",
					accelerator: "CommandOrControl+W",
					click: () => {
						const { activeTabId } = pane.tabStore.getState();

						if (activeTabId) {
							pane.getProfileForTab(activeTabId)?.tabs.close(activeTabId);
						}
					},
				},
				{
					label: "Reopen closed tab",
					accelerator: "CommandOrControl+Shift+T",
					click: () => {
						const closedTab = pane.tabStore.getState().popClosedTab();

						if (closedTab) {
							pane.hideAllTabs();

							pane
								.getOrCreateProfile(closedTab.profileId)
								.tabs.open(closedTab.url);

							pane.navigateToBrowser();
						}
					},
				},
				{ type: "separator" },
				{ role: "close", registerAccelerator: false },
			],
		},
		{ role: "editMenu" },
		{
			label: "View",
			submenu: [
				{
					label: "Find in page",
					accelerator: "CommandOrControl+F",
					click: () => hotkeyEmitter.emitHotkey(HotkeyEvent.FIND_IN_PAGE),
				},
				{ type: "separator" },
				{
					label: "Reload tab",
					accelerator: "CommandOrControl+R",
					click: () => pane.getActiveTabContents()?.reload(),
				},
				{ type: "separator" },
				{
					label: "Toggle developer tools",
					accelerator: "CommandOrControl+Option+I",
					click: () => chrome.webContents.openDevTools({ mode: "detach" }),
				},
				{
					label: "Toggle page developer tools",
					accelerator: "CommandOrControl+Shift+I",
					click: () =>
						pane.getActiveTabContents()?.openDevTools({ mode: "detach" }),
				},
				{ type: "separator" },
				{ role: "resetZoom" },
				{ role: "zoomIn" },
				{ role: "zoomOut" },
				{ type: "separator" },
				{ role: "togglefullscreen" },
			],
		},
		{
			label: "Navigate",
			submenu: [
				{
					label: "Back",
					accelerator: "CommandOrControl+[",
					click: () => pane.getActiveTabContents()?.goBack(),
				},
				{
					label: "Forward",
					accelerator: "CommandOrControl+]",
					click: () => pane.getActiveTabContents()?.goForward(),
				},
				{ type: "separator" },
				{
					label: "Focus address bar",
					accelerator: "CommandOrControl+L",
					click: () => hotkeyEmitter.emitHotkey(HotkeyEvent.FOCUS_ADDRESS_BAR),
				},
			],
		},
		{
			label: "Tab",
			submenu: [
				{
					label: "Next tab (MRU)",
					accelerator: "Control+Tab",
					click: () =>
						openTabSwitcher(
							surface,
							hotkeyEmitter,
							HotkeyEvent.TAB_SWITCHER_FORWARD,
						),
				},
				{
					label: "Previous tab (MRU)",
					accelerator: "Control+Shift+Tab",
					click: () =>
						openTabSwitcher(
							surface,
							hotkeyEmitter,
							HotkeyEvent.TAB_SWITCHER_BACKWARD,
						),
				},
				{ type: "separator" },
				...Array.from({ length: 9 }, (_, index) => ({
					label: `Tab ${index + 1}`,
					accelerator: `CommandOrControl+${index + 1}`,
					click: () => pane.switchToTabByIndex(index),
				})),
			],
		},
		{ role: "windowMenu" },
	]);

	Menu.setApplicationMenu(menu);
}
