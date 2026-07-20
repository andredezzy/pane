import path from "node:path";
import { ElectronChromeExtensions } from "@pane/electron-chrome-extensions";
import {
	app,
	type BrowserWindow,
	nativeTheme,
	session,
	type WebContentsView,
} from "electron";

app.commandLine.appendSwitch("log-level", "3");

// UA Client Hints stay ENABLED so Chromium emits Sec-CH-UA-* headers; each
// profile's onBeforeSendHeaders rewrites them to match its fingerprint (keeping the
// HTTP surface consistent with the spoofed navigator.userAgentData). Disabling them
// would leave a Chrome that exposes userAgentData in JS but sends no hints — itself
// a tell.
app.commandLine.appendSwitch("disable-features", "CalculateNativeWinOcclusion");

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
import { sidebarStore } from "../stores/sidebar-store";
import { tabStore } from "../stores/tab-store";
import { updateStore } from "../stores/update-store";
import { FindEmitter } from "./emitters/find-emitter";
import { HotkeyEmitter } from "./emitters/hotkey-emitter";
import { ExtensionInstaller } from "./extensions";
import { Pane } from "./pane";
import { SleepScheduler } from "./sleep-scheduler";
import { createIPCHandler } from "./trpc/ipc";
import { appRouter } from "./trpc/router";
import { scheduleUpdateChecks } from "./updater";
import type { AppWindow } from "./window";
import { createAppWindow, createMenu } from "./window";

if (!app.requestSingleInstanceLock()) {
	app.quit();
}

let appWindow: AppWindow | null = null;
let pane: Pane | null = null;

function setup() {
	appWindow = createAppWindow();

	const findEmitter = new FindEmitter();
	const hotkeyEmitter = new HotkeyEmitter();

	const currentPane = new Pane(appWindow.mainWindow, findEmitter);
	pane = currentPane;

	const sleepScheduler = new SleepScheduler(currentPane);

	ElectronChromeExtensions.handleCRXProtocol(session.defaultSession);

	createMenu(
		appWindow.chrome,
		currentPane,
		hotkeyEmitter,
		appWindow.surface,
		updateStore,
	);

	const createContext = () => ({
		pane: currentPane,
		chrome: appWindow?.chrome as WebContentsView,
		surface: appWindow?.surface as BrowserWindow,
		hotkeyEmitter,
		findEmitter,
		stores: {
			"profile-store": profileStore,
			"tab-store": tabStore,
			"navigation-store": navigationStore,
			"settings-store": settingsStore,
			"extension-store": extensionStore,
			"security-store": securityStore,
			"sidebar-store": sidebarStore,
			"update-store": updateStore,
		},
	});

	const cleanupChromeIPC = createIPCHandler({
		router: appRouter,
		webContents: appWindow.chrome.webContents,
		createContext,
	});

	const cleanupSurfaceIPC = createIPCHandler({
		router: appRouter,
		webContents: appWindow.surface.webContents,
		createContext,
	});

	function syncBounds() {
		const [width, height] = appWindow?.mainWindow.getContentSize() ?? [0, 0];
		const bounds = appWindow?.mainWindow.getBounds();

		appWindow?.chrome.setBounds({ x: 0, y: 0, width, height });

		if (bounds) {
			appWindow?.surface.setBounds(bounds);
		}

		pane?.resizeAllTabs();
	}

	appWindow.mainWindow.on("resized", syncBounds);
	appWindow.mainWindow.on("moved", syncBounds);
	appWindow.mainWindow.on("maximize", syncBounds);
	appWindow.mainWindow.on("unmaximize", syncBounds);
	appWindow.mainWindow.on("enter-full-screen", syncBounds);
	appWindow.mainWindow.on("leave-full-screen", syncBounds);

	appWindow.mainWindow.on("closed", () => {
		sleepScheduler.dispose();
		pane?.destroy();

		appWindow?.surface.destroy();

		cleanupChromeIPC();
		cleanupSurfaceIPC();

		appWindow = null;
		pane = null;
	});

	pane.restore();
}

app.on("second-instance", () => {
	if (appWindow) {
		if (appWindow.mainWindow.isMinimized()) {
			appWindow.mainWindow.restore();
		}

		appWindow.mainWindow.focus();
	}
});

app.whenReady().then(() => {
	profileStore.persist.rehydrate();
	settingsStore.persist.rehydrate();
	securityStore.persist.rehydrate();

	// Applied before any window exists so the first paint — and every web
	// contents' prefers-color-scheme — already matches the persisted theme.
	nativeTheme.themeSource = settingsStore.getState().settings.theme;

	settingsStore.subscribe((state, previous) => {
		if (state.settings.theme !== previous.settings.theme) {
			nativeTheme.themeSource = state.settings.theme;
		}
	});

	if (securityStore.getState().pin !== null) {
		securityStore.getState().lock();
	}

	ExtensionInstaller.registerProtocol(
		path.join(app.getPath("userData"), "Extensions"),
	);

	scheduleUpdateChecks(updateStore);

	setup();

	app.on("activate", () => {
		if (!appWindow) {
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
