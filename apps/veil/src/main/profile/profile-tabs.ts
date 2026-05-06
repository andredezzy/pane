import { type BaseWindow, type WebContents, WebContentsView } from "electron";
import {
	PANEL_MARGIN_BOTTOM,
	PANEL_MARGIN_RIGHT,
	SIDEBAR_WIDTH,
	TOOLBAR_HEIGHT,
} from "../../constants/layout";
import {
	type BrowserProfile,
	Platform,
	profileStore,
} from "../../stores/profile-store";
import { tabStore } from "../../stores/tab-store";

export interface TabHost {
	readonly id: string;
	readonly session: Electron.Session;
	readonly ece: import("@pane/electron-chrome-extensions").ElectronChromeExtensions;
	readonly extensions: { ensureLoaded(): Promise<void> };
	get data(): BrowserProfile;
	onTabOpened(tabId: string): void;
	onTabClosed(tabId: string): void;
}

export class ProfileTabs {
	private readonly views = new Map<string, WebContentsView>();

	constructor(
		private readonly profile: TabHost,
		private readonly mainWindow: BaseWindow,
	) {}

	open(url?: string, tabId?: string): WebContentsView {
		const id = tabId ?? crypto.randomUUID();
		const targetUrl = url || "https://www.google.com";
		const view = this.createView(id);

		this.hideAll();

		this.views.set(id, view);
		this.profile.onTabOpened(id);
		this.mainWindow.contentView.addChildView(view);

		this.applyFingerprint(view.webContents, this.profile.data.fingerprint);
		view.webContents.loadURL(targetUrl);

		this.profile.extensions.ensureLoaded();
		this.profile.ece.addTab(view.webContents, this.mainWindow);

		profileStore.getState().openTab(this.profile.id, id, targetUrl);
		this.activate(id);

		return view;
	}

	close(tabId: string): void {
		const view = this.views.get(tabId);

		if (!view) {
			return;
		}

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

		this.views.delete(tabId);
		tabStore.getState().setLoading(tabId, false);
		tabStore.getState().removeMru(tabId);

		this.profile.onTabClosed(tabId);
		profileStore.getState().closeTab(this.profile.id, tabId);

		if (tabStore.getState().activeTabId === tabId) {
			const remainingTabs = this.profile.data.tabs;
			const nextTab = remainingTabs[remainingTabs.length - 1];

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
				this.applyFingerprint(view.webContents, this.profile.data.fingerprint);
				view.webContents.loadURL(tab.url);
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
		this.activeView()?.webContents.loadURL(normalizeUrl(url));
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

	stop(): void {
		this.activeView()?.webContents.stop();
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

		this.views.set(tabId, view);
		this.profile.onTabOpened(tabId);
		this.mainWindow.contentView.addChildView(view);

		this.applyFingerprint(view.webContents, this.profile.data.fingerprint);
		view.webContents.loadURL(url);

		this.profile.ece.addTab(view.webContents, this.mainWindow);

		profileStore.getState().openTab(this.profile.id, tabId, url);

		this.activate(tabId);

		return view;
	}

	private activeView(): WebContentsView | undefined {
		const { activeTabId } = tabStore.getState();

		return activeTabId ? this.views.get(activeTabId) : undefined;
	}

	private static readonly FIREFOX_UA: Record<Platform, string> = {
		[Platform.WINDOWS]:
			"Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:132.0) Gecko/20100101 Firefox/132.0",
		[Platform.MACOS]:
			"Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:132.0) Gecko/20100101 Firefox/132.0",
		[Platform.LINUX]:
			"Mozilla/5.0 (X11; Linux x86_64; rv:132.0) Gecko/20100101 Firefox/132.0",
	};

	private applyFingerprint(
		webContents: WebContents,
		fingerprint: BrowserProfile["fingerprint"],
	): void {
		webContents.setUserAgent(
			ProfileTabs.FIREFOX_UA[fingerprint.platform] ??
				ProfileTabs.FIREFOX_UA[Platform.MACOS],
		);

		webContents.on("dom-ready", () => {
			if (webContents.isDestroyed()) {
				return;
			}

			const pageUrl = webContents.getURL();

			if (
				pageUrl.startsWith("chrome-extension:") ||
				pageUrl.startsWith("chrome:")
			) {
				return;
			}

			webContents
				.executeJavaScript(ProfileTabs.FIREFOX_SPOOF_SCRIPT)
				.catch(() => {});
		});
	}

	private static readonly FIREFOX_SPOOF_SCRIPT = `(function(){
		try{Object.defineProperty(Navigator.prototype,'vendor',{get:function(){return''},configurable:true})}catch(e){}
		try{Object.defineProperty(Navigator.prototype,'productSub',{get:function(){return'20100101'},configurable:true})}catch(e){}
		try{Object.defineProperty(Navigator.prototype,'userAgentData',{get:function(){return undefined},configurable:true})}catch(e){}
		try{delete Navigator.prototype.getBattery}catch(e){}
		try{delete window.chrome;Object.defineProperty(window,'chrome',{get:function(){return undefined},configurable:true})}catch(e){}
		try{var o=CSS.supports.bind(CSS);CSS.supports=function(a,b){if(arguments.length===1){if(a&&a.indexOf('-moz-')!==-1)return true;if(a&&a.indexOf('-webkit-app-region')!==-1)return false;return o(a)}if(a&&a.indexOf&&a.indexOf('-moz-')!==-1)return true;if(a==='-webkit-app-region'||a==='-webkit-tap-highlight-color')return false;return o(a,b)}}catch(e){}
	})()`;

	private retryGoogleLogin(
		view: WebContentsView,
		url: string,
		retries: { count: number },
	): boolean {
		if (
			!url.includes("accounts.google.com") ||
			!url.includes("/rejected") ||
			retries.count >= 5
		) {
			return false;
		}

		retries.count++;

		const continueParam =
			new URL(url).searchParams.get("continue") || "https://www.google.com/";

		const loginUrl = `https://accounts.google.com/ServiceLogin?continue=${encodeURIComponent(continueParam)}`;

		this.profile.session
			.clearStorageData({
				origin: "https://accounts.google.com",
				storages: ["cookies", "localstorage"],
			})
			.then(() => {
				if (!view.webContents.isDestroyed()) {
					view.webContents.loadURL(loginUrl);
				}
			})
			.catch(() => {});

		return true;
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
		const googleRetries = { count: 0 };

		view.webContents.on("did-navigate", (_e, url) => {
			if (this.retryGoogleLogin(view, url, googleRetries)) {
				return;
			}

			if (url.includes("accounts.google.com") && !url.includes("/rejected")) {
				googleRetries.count = 0;
			}

			profileStore.getState().updateTab(profileId, tabId, { url });
		});

		view.webContents.on("did-navigate-in-page", (_e, url, isMainFrame) => {
			if (!isMainFrame) {
				return;
			}

			if (this.retryGoogleLogin(view, url, googleRetries)) {
				return;
			}

			profileStore.getState().updateTab(profileId, tabId, { url });
		});

		view.webContents.on("page-title-updated", (_e, title) => {
			profileStore.getState().updateTab(profileId, tabId, { title });
		});

		view.webContents.on("page-favicon-updated", (_e, favicons) => {
			profileStore.getState().updateTab(profileId, tabId, {
				favicon: favicons[0] ?? "",
			});
		});

		view.webContents.on("did-start-loading", () => {
			tabStore.getState().setLoading(tabId, true);
		});

		view.webContents.on("did-stop-loading", () => {
			tabStore.getState().setLoading(tabId, false);
		});

		view.webContents.setWindowOpenHandler(({ url }) => {
			this.open(url);

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

function normalizeUrl(url: string): string {
	if (url.startsWith("http://") || url.startsWith("https://")) {
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
