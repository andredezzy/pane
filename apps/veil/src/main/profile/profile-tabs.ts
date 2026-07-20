import { buildChromeContextMenu } from "@pane/electron-chrome-context-menu";
import { type BaseWindow, type WebContents, WebContentsView } from "electron";
import {
	PANEL_MARGIN_BOTTOM,
	PANEL_MARGIN_RIGHT,
	SIDEBAR_WIDTH,
	TOOLBAR_HEIGHT,
} from "../../constants/layout";
import { type BrowserProfile, profileStore } from "../../stores/profile-store";
import { tabStore } from "../../stores/tab-store";
import type { FindEmitter } from "../emitters/find-emitter";
import { fetchFaviconAsDataUrl } from "./favicon";
import { isGoogleUrl } from "./google/domains";
import { GoogleSignIn } from "./google/sign-in";
import { mostRecentTab } from "./mru";

export interface TabHost {
	readonly id: string;
	readonly session: Electron.Session;
	readonly ece: import("@pane/electron-chrome-extensions").ElectronChromeExtensions;
	readonly extensions: { ensureLoaded(): Promise<void> };
	readonly proxyReady: Promise<boolean>;
	get data(): BrowserProfile;
	onTabOpened(tabId: string): void;
	onTabClosed(tabId: string): void;
}

interface FindOptions {
	forward?: boolean;
	findNext?: boolean;
}

export class ProfileTabs {
	private readonly views = new Map<string, WebContentsView>();

	constructor(
		private readonly profile: TabHost,
		private readonly mainWindow: BaseWindow,
		private readonly findEmitter: FindEmitter,
	) {}

	open(url?: string | null, tabId?: string): WebContentsView {
		const id = tabId ?? crypto.randomUUID();
		const targetUrl = url === null ? null : url || "https://www.google.com";
		const view = this.createView(id);

		this.hideAll();

		this.views.set(id, view);
		this.profile.onTabOpened(id);
		this.mainWindow.contentView.addChildView(view);

		if (targetUrl) {
			this.safeLoadURL(view.webContents, targetUrl);
		}

		this.profile.extensions.ensureLoaded();
		this.profile.ece.addTab(view.webContents, this.mainWindow);

		profileStore.getState().openTab(this.profile.id, id, targetUrl ?? "");
		this.activate(id);

		return view;
	}

	close(tabId: string): void {
		const view = this.views.get(tabId);

		if (!view) {
			return;
		}

		// Delete the entry FIRST: ece.removeTab() synchronously re-enters close() via
		// the ECE impl.removeTab -> destroyByWebContents callback. With the tab already
		// gone from the map, that reentrant call hits the `!view` guard and no-ops —
		// otherwise the body runs twice (duplicate pushClosedTab + a second
		// webContents.close() on the already-destroyed contents, which throws).
		this.views.delete(tabId);

		const tabData = this.profile.data.tabs.find((tab) => tab.id === tabId);

		if (tabData?.url) {
			tabStore.getState().pushClosedTab({
				url: tabData.url,
				profileId: this.profile.id,
				title: tabData.title || undefined,
				favicon: tabData.favicon || undefined,
			});
		}

		this.profile.ece.removeTab(view.webContents);
		this.mainWindow.contentView.removeChildView(view);
		view.webContents.close();

		tabStore.getState().setLoading(tabId, false);
		tabStore.getState().removeMru(tabId);

		this.profile.onTabClosed(tabId);
		profileStore.getState().closeTab(this.profile.id, tabId);

		if (tabStore.getState().activeTabId === tabId) {
			// Closing the active tab lands on the most recently used remaining tab in
			// this profile (the global MRU history naturally skips other profiles).
			const nextTab = mostRecentTab(
				this.profile.data.tabs,
				tabStore.getState().mruHistory,
			);

			if (nextTab) {
				this.activate(nextTab.id);
			} else {
				tabStore.getState().setActiveTab(null, null);
			}
		}
	}

	activate(tabId: string): void {
		let view = this.views.get(tabId);

		if (!view) {
			const tab = this.profile.data.tabs.find((tab) => tab.id === tabId);

			if (tab) {
				view = this.createView(tabId);
				this.views.set(tabId, view);

				this.safeLoadURL(view.webContents, tab.url);
			}
		}

		if (view) {
			if (!this.mainWindow.contentView.children.includes(view)) {
				this.mainWindow.contentView.addChildView(view);
				this.profile.ece.addTab(view.webContents, this.mainWindow);
				this.profile.extensions.ensureLoaded();
			}

			view.setVisible(true);
			this.profile.ece.selectTab(view.webContents);
		}

		tabStore.getState().setActiveTab(tabId, this.profile.id);
		tabStore.getState().pushMru(tabId);
	}

	closeAll(): void {
		for (const tabId of [...this.views.keys()]) {
			this.close(tabId);
		}
	}

	unloadAll(): void {
		const views = [...this.views.entries()];
		this.views.clear();

		for (const [tabId, view] of views) {
			tabStore.getState().setLoading(tabId, false);

			try {
				this.mainWindow.contentView.removeChildView(view);
				view.webContents.close();
			} catch {}
		}
	}

	destroyAll(): void {
		for (const view of this.views.values()) {
			try {
				this.mainWindow.contentView.removeChildView(view);
				view.webContents.close();
			} catch {}
		}

		this.views.clear();
	}

	resizeAll(): void {
		const bounds = this.getContentBounds();
		for (const view of this.views.values()) {
			view.setBounds(bounds);
		}
	}

	navigate(url: string): void {
		const webContents = this.activeView()?.webContents;

		if (webContents) {
			this.safeLoadURL(webContents, normalizeUrl(url));
		}
	}

	goBack(): void {
		this.activeView()?.webContents.goBack();
	}

	goForward(): void {
		this.activeView()?.webContents.goForward();
	}

	reload(): void {
		this.activeView()?.webContents.reload();
	}

	reloadGoogleTabs(): void {
		for (const view of this.views.values()) {
			if (view.webContents.isDestroyed()) {
				continue;
			}

			if (isGoogleUrl(view.webContents.getURL())) {
				view.webContents.reload();
			}
		}
	}

	stop(): void {
		this.activeView()?.webContents.stop();
	}

	find(text: string, options?: FindOptions): void {
		this.activeView()?.webContents.findInPage(text, options);
	}

	stopFind(): void {
		this.activeView()?.webContents.stopFindInPage("clearSelection");
	}

	hideAll(): void {
		for (const view of this.views.values()) {
			view.setVisible(false);
		}
	}

	showActive(): void {
		this.activeView()?.setVisible(true);
	}

	getActiveWebContents(): WebContents | undefined {
		return this.activeView()?.webContents;
	}

	has(tabId: string): boolean {
		return (
			this.views.has(tabId) ||
			this.profile.data.tabs.some((tab) => tab.id === tabId)
		);
	}

	activateByWebContents(webContents: WebContents): void {
		for (const [tabId, view] of this.views) {
			if (view.webContents === webContents) {
				this.activate(tabId);

				return;
			}
		}
	}

	destroyByWebContents(webContents: WebContents): void {
		for (const [tabId, view] of this.views) {
			if (view.webContents === webContents) {
				this.close(tabId);

				return;
			}
		}
	}

	openForExtension(url: string): WebContentsView {
		const tabId = crypto.randomUUID();
		const view = this.createView(tabId);

		this.hideAll();

		this.views.set(tabId, view);
		this.profile.onTabOpened(tabId);
		this.mainWindow.contentView.addChildView(view);

		this.safeLoadURL(view.webContents, url);

		this.profile.ece.addTab(view.webContents, this.mainWindow);

		profileStore.getState().openTab(this.profile.id, tabId, url);

		this.activate(tabId);

		return view;
	}

	private safeLoadURL(webContents: WebContents, url: string): void {
		this.profile.proxyReady.then((ready) => {
			if (!ready || webContents.isDestroyed()) {
				return;
			}

			// loadURL rejects on any non-aborted navigation failure (DNS, network,
			// cert, proxy). ERR_ABORTED is the normal "superseded by a newer load"
			// case; surface the rest instead of leaking an unhandled rejection.
			webContents.loadURL(url).catch((error: Error) => {
				if (!error.message.includes("ERR_ABORTED")) {
					console.warn(
						`[Profile ${this.profile.id}] Navigation failed:`,
						error.message,
					);
				}
			});
		});
	}

	private activeView(): WebContentsView | undefined {
		const { activeTabId } = tabStore.getState();

		return activeTabId ? this.views.get(activeTabId) : undefined;
	}

	private createView(tabId: string): WebContentsView {
		const view = new WebContentsView({
			webPreferences: {
				session: this.profile.session,
				contextIsolation: true,
				sandbox: true,
			},
		});

		const profileId = this.profile.id;
		const googleSignIn = new GoogleSignIn(view, this.profile.session);

		// WebRTC ICE candidates leak the real LAN IP / mDNS hostname past the proxy
		// (its rules only govern TCP). Hide private interfaces; a proxied profile
		// forces all UDP through the proxy so nothing escapes around it.
		view.webContents.setWebRTCIPHandlingPolicy(
			this.profile.data.proxy
				? "disable_non_proxied_udp"
				: "default_public_interface_only",
		);

		view.webContents.on("did-navigate", (_e, url) => {
			if (googleSignIn.intercept(url)) {
				return;
			}

			profileStore.getState().updateTab(profileId, tabId, { url });
		});

		view.webContents.on("did-navigate-in-page", (_e, url, isMainFrame) => {
			if (!isMainFrame) {
				return;
			}

			if (googleSignIn.intercept(url)) {
				return;
			}

			profileStore.getState().updateTab(profileId, tabId, { url });
		});

		view.webContents.on("page-title-updated", (_e, title) => {
			profileStore.getState().updateTab(profileId, tabId, { title });
		});

		view.webContents.on("page-favicon-updated", (_e, favicons) => {
			const faviconUrl = favicons[0] ?? "";

			// Show the remote reference immediately, then replace it with an inline
			// data URL fetched through THIS profile's session — the request rides the
			// profile's proxy/fingerprint, and the sidebar renders instantly from the
			// store afterwards instead of re-fetching on every profile expand.
			profileStore.getState().updateTab(profileId, tabId, {
				favicon: faviconUrl,
			});

			if (!faviconUrl || faviconUrl.startsWith("data:")) {
				return;
			}

			fetchFaviconAsDataUrl(view.webContents.session, faviconUrl)
				.then((dataUrl) => {
					if (dataUrl) {
						profileStore.getState().updateTab(profileId, tabId, {
							favicon: dataUrl,
						});
					}
				})
				.catch(() => {
					// The remote URL already in the store remains the fallback.
				});
		});

		view.webContents.on("did-start-loading", () => {
			tabStore.getState().setLoading(tabId, true);
		});

		view.webContents.on("did-stop-loading", () => {
			tabStore.getState().setLoading(tabId, false);
		});

		view.webContents.on("found-in-page", (_e, result) => {
			this.findEmitter.emitResult({
				activeMatchOrdinal: result.activeMatchOrdinal,
				matches: result.matches,
			});
		});

		view.webContents.on("context-menu", (_event, params) => {
			const menu = buildChromeContextMenu({
				params,
				webContents: view.webContents,
				openLink: (url) => {
					if (isWebUrl(url)) {
						this.open(url);
					}
				},
				extensionMenuItems: this.profile.ece.getContextMenuItems(
					view.webContents,
					params,
				),
			});

			menu.popup();
		});

		// A renderer can pass any scheme here (window.open) or via a crafted link's
		// href (context-menu "open in new tab"). open() -> loadURL() runs in the main
		// process and is NOT subject to the renderer's file:// navigation block, so an
		// unguarded open() would read local files; restrict both paths to web URLs.
		view.webContents.setWindowOpenHandler(({ url }) => {
			if (isWebUrl(url)) {
				this.open(url);
			}

			return { action: "deny" };
		});

		view.setBounds(this.getContentBounds());
		view.setBorderRadius(10);
		view.setVisible(false);

		return view;
	}

	private getContentBounds(): Electron.Rectangle {
		const [width, height] = this.mainWindow.getContentSize();

		return {
			x: SIDEBAR_WIDTH,
			y: TOOLBAR_HEIGHT,
			width: width - SIDEBAR_WIDTH - PANEL_MARGIN_RIGHT,
			height: height - TOOLBAR_HEIGHT - PANEL_MARGIN_BOTTOM,
		};
	}
}

function isWebUrl(url: string): boolean {
	return url.startsWith("http://") || url.startsWith("https://");
}

function normalizeUrl(url: string): string {
	if (isWebUrl(url)) {
		return url;
	}

	if (
		url.startsWith("localhost") ||
		(url.includes(".") && !url.includes(" "))
	) {
		return `https://${url}`;
	}

	return `https://www.google.com/search?q=${encodeURIComponent(url)}`;
}
