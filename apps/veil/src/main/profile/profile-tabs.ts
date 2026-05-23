import { buildChromeContextMenu } from "@pane/electron-chrome-context-menu";
import { type BaseWindow, type WebContents, WebContentsView } from "electron";
import {
	importCookiesViaCdp,
	launchChromeForGoogleAuth,
} from "./google-auth-window";
import {
	PANEL_MARGIN_BOTTOM,
	PANEL_MARGIN_RIGHT,
	SIDEBAR_WIDTH,
	TOOLBAR_HEIGHT,
} from "../../constants/layout";
import {
	type BrowserProfile,
	profileStore,
} from "../../stores/profile-store";
import { tabStore } from "../../stores/tab-store";
import type { FindEmitter } from "../emitters/find-emitter";

const GOOGLE_AUTH_PAGE = `data:text/html,${encodeURIComponent(`<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body {
	font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", system-ui, sans-serif;
	display: flex; flex-direction: column; align-items: center; justify-content: center;
	min-height: 100vh; background: #0a0a0a; color: #e4e4e7;
	padding: 40px 24px; gap: 32px;
}
svg.logo { width: 40px; height: 40px; }
h1 { font-size: 28px; font-weight: 500; letter-spacing: -0.03em; color: #fafafa; }
.desc { font-size: 15px; color: #71717a; line-height: 1.6; max-width: 480px; text-align: center; }
ol {
	list-style: none; counter-reset: steps;
	display: flex; flex-direction: column; gap: 12px;
	max-width: 400px; width: 100%; margin: 8px 0;
}
li {
	counter-increment: steps; font-size: 14px; color: #a1a1aa;
	line-height: 1.5; padding-left: 32px; position: relative;
}
li::before {
	content: counter(steps); position: absolute; left: 0; top: 1px;
	width: 20px; height: 20px; border-radius: 50%;
	background: #27272a; color: #71717a; font-size: 11px;
	display: flex; align-items: center; justify-content: center;
}
button {
	padding: 14px 40px; font-size: 15px; font-weight: 500;
	border-radius: 10px; border: none; cursor: pointer;
	font-family: inherit; transition: all 0.15s ease;
	background: #fafafa; color: #09090b; margin-top: 8px;
}
button:hover { background: #e4e4e7; }
button:active { transform: scale(0.98); }
button:disabled { opacity: 0.4; cursor: not-allowed; transform: none; }
#err { color: #f87171; font-size: 13px; display: none; max-width: 400px; text-align: center; }
</style></head>
<body>
<svg class="logo" viewBox="0 0 48 48">
	<path fill="#EA4335" d="M24 9.5c3.54 0 6.71 1.22 9.21 3.6l6.85-6.85C35.9 2.38 30.47 0 24 0 14.62 0 6.51 5.38 2.56 13.22l7.98 6.19C12.43 13.72 17.74 9.5 24 9.5z"/>
	<path fill="#4285F4" d="M46.98 24.55c0-1.57-.15-3.09-.38-4.55H24v9.02h12.94c-.58 2.96-2.26 5.48-4.78 7.18l7.73 6c4.51-4.18 7.09-10.36 7.09-17.65z"/>
	<path fill="#34A853" d="M10.53 28.59A14.5 14.5 0 019.5 24c0-1.59.28-3.14.76-4.59l-7.98-6.19C.92 16.46 0 20.12 0 24c0 3.88.92 7.54 2.56 10.78l7.97-6.19z"/>
	<path fill="#FBBC05" d="M24 48c6.48 0 11.93-2.13 15.89-5.81l-7.73-6c-2.15 1.45-4.92 2.3-8.16 2.3-6.26 0-11.57-4.22-13.47-9.91l-7.98 6.19C6.51 42.62 14.62 48 24 48z"/>
</svg>
<h1>Sign in with Google Chrome</h1>
<p class="desc">Google requires signing in through a supported browser. A Chrome window has been opened for you.</p>
<ol>
	<li>Sign in to your Google account in the Chrome window</li>
	<li>Once signed in, come back to Pane</li>
	<li>Click the button below to transfer your session</li>
</ol>
<button id="btn" onclick="this.disabled=true;this.textContent='Transferring session...';console.log('__PANE_TRANSFER__')">
	Transfer session to Pane
</button>
<p id="err"></p>
</body>
</html>`)}`;

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

export class ProfileTabs {
	private readonly views = new Map<string, WebContentsView>();

	constructor(
		private readonly profile: TabHost,
		private readonly mainWindow: BaseWindow,
		private readonly findEmitter: FindEmitter,
	) {}

	open(url?: string | null, tabId?: string): WebContentsView {
		const id = tabId ?? crypto.randomUUID();
		const targetUrl = url === null ? null : (url || "https://www.google.com");
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

	stop(): void {
		this.activeView()?.webContents.stop();
	}

	find(
		text: string,
		options?: { forward?: boolean; findNext?: boolean },
	): void {
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

			webContents.loadURL(url);
		});
	}

	private activeView(): WebContentsView | undefined {
		const { activeTabId } = tabStore.getState();

		return activeTabId ? this.views.get(activeTabId) : undefined;
	}

	private handleGoogleRejection(
		view: WebContentsView,
		url: string,
		state: { pending: boolean },
	): boolean {
		if (
			state.pending ||
			!url.includes("accounts.google.com") ||
			!url.includes("/rejected")
		) {
			return false;
		}

		state.pending = true;

		const continueUrl =
			new URL(url).searchParams.get("continue") || "https://www.google.com/";

		if (!launchChromeForGoogleAuth(continueUrl)) {
			state.pending = false;
			return false;
		}

		if (!view.webContents.isDestroyed()) {
			view.webContents.loadURL(GOOGLE_AUTH_PAGE);
		}

		view.webContents.on("console-message", (_e, _level, message) => {
			if (message !== "__PANE_TRANSFER__") {
				return;
			}

			this.transferGoogleSession(view, continueUrl, state);
		});

		return true;
	}

	private transferGoogleSession(
		view: WebContentsView,
		continueUrl: string,
		state: { pending: boolean },
	): void {
		importCookiesViaCdp(this.profile.session)
			.then((count) => {
				state.pending = false;

				if (count === 0) {
					this.showAuthError(
						view,
						"No Google cookies found. Make sure you completed sign-in in the Chrome window.",
					);
					return;
				}

				if (!view.webContents.isDestroyed()) {
					view.webContents.loadURL(continueUrl);
				}
			})
			.catch((error: Error) => {
				this.showAuthError(view, error.message);
			});
	}

	private showAuthError(view: WebContentsView, message: string): void {
		if (view.webContents.isDestroyed()) return;

		view.webContents.executeJavaScript(`
			document.getElementById("btn").disabled = false;
			document.getElementById("btn").textContent = "Transfer session to Pane";
			var err = document.getElementById("err");
			err.style.display = "block";
			err.textContent = "${message.replace(/"/g, '\\"')}";
		`).catch(() => {});
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
		const googleAuth = { pending: false };

		view.webContents.on("did-navigate", (_e, url) => {
			if (this.handleGoogleRejection(view, url, googleAuth)) {
				return;
			}

			profileStore.getState().updateTab(profileId, tabId, { url });
		});

		view.webContents.on("did-navigate-in-page", (_e, url, isMainFrame) => {
			if (!isMainFrame) {
				return;
			}

			if (this.handleGoogleRejection(view, url, googleAuth)) {
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
				openLink: (url) => this.open(url),
				extensionMenuItems: this.profile.ece.getContextMenuItems(
					view.webContents,
					params,
				),
			});

			menu.popup();
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
