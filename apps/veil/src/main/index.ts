import path from "node:path";
import { ElectronChromeExtensions } from "@pane/electron-chrome-extensions";
import { app, type BrowserWindow } from "electron";

app.commandLine.appendSwitch("log-level", "3");

app.commandLine.appendSwitch(
	"disable-features",
	"UserAgentClientHint,ClientHintThirdPartyDelegation",
);

app.commandLine.appendSwitch("disable-blink-features", "AutomationControlled");

app.userAgentFallback = app.userAgentFallback
	.replace(/\s*Electron\/\S+/g, "")
	.replace(/\s*@?pane\/\S+/gi, "")
	.replace(/\s{2,}/g, " ");

ElectronChromeExtensions.prepareUserData(app.getPath("userData"));

import { extensionStore } from "../stores/extension-store";
import { flushPendingWrites } from "../stores/middlewares/fs-storage";
import { navigationStore } from "../stores/navigation-store";
import { profileStore } from "../stores/profile-store";
import { securityStore } from "../stores/security-store";
import { settingsStore } from "../stores/settings-store";
import { tabStore } from "../stores/tab-store";
import { detectBrowserPath } from "./detect-browser";
import { ExtensionInstaller } from "./extension-installer";
import { Pane } from "./pane";
import { createIPCHandler } from "./trpc/ipc";
import { appRouter } from "./trpc/router";
import type { AppWindow } from "./window";
import { createAppWindow, createMenu } from "./window";

if (!app.requestSingleInstanceLock()) {
	app.quit();
}

let win: AppWindow | null = null;
let pane: Pane | null = null;

function setup() {
	win = createAppWindow();

	const currentPane = new Pane(win.mainWindow);
	pane = currentPane;

	createMenu(win.chrome, () => currentPane.getActiveTabContents());

	const createContext = () => ({
		pane: currentPane,
		surface: win?.surface as BrowserWindow,
		stores: {
			"profile-store": profileStore,
			"tab-store": tabStore,
			"navigation-store": navigationStore,
			"settings-store": settingsStore,
			"extension-store": extensionStore,
			"security-store": securityStore,
		},
	});

	const cleanupChromeIPC = createIPCHandler({
		router: appRouter,
		webContents: win.chrome.webContents,
		createContext,
	});

	const cleanupSurfaceIPC = createIPCHandler({
		router: appRouter,
		webContents: win.surface.webContents,
		createContext,
	});

	function syncBounds() {
		const [w, h] = win?.mainWindow.getContentSize() ?? [0, 0];
		win?.chrome.setBounds({ x: 0, y: 0, width: w, height: h });
		win?.surface.setSize(w, h);
		pane?.resizeAllTabs();
	}

	win.mainWindow.on("resized", syncBounds);
	win.mainWindow.on("maximize", syncBounds);
	win.mainWindow.on("unmaximize", syncBounds);
	win.mainWindow.on("enter-full-screen", syncBounds);
	win.mainWindow.on("leave-full-screen", syncBounds);

	win.mainWindow.on("closed", () => {
		if (pane) {
			for (const profile of pane.profiles.values()) {
				profile.tabs.destroyAll();
			}
		}

		win?.surface.destroy();
		cleanupChromeIPC();
		cleanupSurfaceIPC();
		win = null;
		pane = null;
	});

	pane.restore();
}

app.on("second-instance", () => {
	if (win) {
		if (win.mainWindow.isMinimized()) {
			win.mainWindow.restore();
		}

		win.mainWindow.focus();
	}
});

app.whenReady().then(() => {
	profileStore.persist.rehydrate();
	settingsStore.persist.rehydrate();
	securityStore.persist.rehydrate();

	if (securityStore.getState().pin !== null) {
		securityStore.getState().lock();
	}

	if (!settingsStore.getState().settings.chromiumPath) {
		const detected = detectBrowserPath();

		if (detected) {
			settingsStore.getState().save({ chromiumPath: detected });
		}
	}

	ExtensionInstaller.registerProtocol(
		path.join(app.getPath("userData"), "Extensions"),
	);

	setup();

	app.on("activate", () => {
		if (!win) {
			setup();
		}
	});
});

app.on("before-quit", () => {
	flushPendingWrites();
});

app.on("window-all-closed", () => {
	if (process.platform !== "darwin") {
		app.quit();
	}
});
