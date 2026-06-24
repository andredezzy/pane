import type { Session, WebContentsView } from "electron";
import { GOOGLE_AUTH_PAGE } from "./auth-page";
import {
	type AuthChromeHandle,
	cleanupAuthChrome,
	importCookiesViaCdp,
	launchChromeForGoogleAuth,
} from "./auth-window";
import { isGoogleUrl } from "./domains";

const TRANSFER_TIMEOUT_MS = 5 * 60 * 1000;

export class GoogleSignIn {
	private pending = false;

	constructor(
		private readonly view: WebContentsView,
		private readonly session: Session,
	) {}

	// Intercepts Google's "this browser may not be secure" rejection: spawns a real
	// Chrome to sign in, shows the transfer page, then pulls the resulting session
	// back into this tab. Returns true when it handled `url` (caller should stop).
	intercept(url: string): boolean {
		if (this.pending) {
			return false;
		}

		let parsed: URL;
		try {
			parsed = new URL(url);
		} catch {
			return false;
		}

		// Hostname-anchored: a bare substring match would let evil.com/accounts.google.
		// com/rejected — or a same-page history.pushState to that path — spawn Chrome
		// and take over the tab. Only the real rejection endpoint qualifies.
		if (
			parsed.hostname !== "accounts.google.com" ||
			!parsed.pathname.includes("/rejected")
		) {
			return false;
		}

		this.pending = true;

		// `continue` is an attacker-influenceable query param that we later load with
		// the freshly imported Google cookies — only honour a real Google https URL.
		const rawContinue = parsed.searchParams.get("continue") ?? "";
		const continueUrl =
			rawContinue.startsWith("https://") && isGoogleUrl(rawContinue)
				? rawContinue
				: "https://www.google.com/";

		const handle = launchChromeForGoogleAuth(continueUrl);

		if (!handle) {
			this.pending = false;
			return false;
		}

		if (!this.view.webContents.isDestroyed()) {
			this.view.webContents.loadURL(GOOGLE_AUTH_PAGE);
		}

		const onConsoleMessage = (
			_e: Electron.Event,
			_level: number,
			message: string,
		) => {
			// Only the transfer page (a data: URL) may trigger the import — otherwise
			// any later page that logs the sentinel could drive a CDP cookie pull.
			if (
				message !== "__PANE_TRANSFER__" ||
				!this.view.webContents.getURL().startsWith("data:text/html")
			) {
				return;
			}

			// Stop listening (the import owns Chrome cleanup from here); transfer.
			stopListening();
			this.transfer(continueUrl, handle);
		};

		const onDestroyed = () => {
			clearTimeout(timer);
			cleanupAuthChrome(handle);
			this.pending = false;
		};

		const stopListening = () => {
			clearTimeout(timer);

			if (!this.view.webContents.isDestroyed()) {
				this.view.webContents.removeListener(
					"console-message",
					onConsoleMessage,
				);
				this.view.webContents.removeListener("destroyed", onDestroyed);
			}
		};

		// Bound the listener's lifetime: if the user never completes the transfer,
		// stop listening, kill the spawned Chrome, and let them retry.
		const timer = setTimeout(() => {
			stopListening();
			cleanupAuthChrome(handle);
			this.pending = false;
		}, TRANSFER_TIMEOUT_MS);

		this.view.webContents.on("console-message", onConsoleMessage);
		this.view.webContents.once("destroyed", onDestroyed);

		return true;
	}

	private transfer(continueUrl: string, handle: AuthChromeHandle): void {
		// importCookiesViaCdp polls Chrome for up to ~10s. stopListening() already
		// removed the intercept-level destroyed handler, so if the tab is destroyed or
		// the app quits during that window the .then/.catch never run — guard the
		// destroyed event here so the detached Chrome + temp dir are still cleaned up.
		let cleaned = false;
		const cleanup = () => {
			if (cleaned) {
				return;
			}

			cleaned = true;
			cleanupAuthChrome(handle);
		};

		const onDestroyed = () => {
			this.pending = false;
			cleanup();
		};

		const stopGuard = () => {
			if (!this.view.webContents.isDestroyed()) {
				this.view.webContents.removeListener("destroyed", onDestroyed);
			}
		};

		if (!this.view.webContents.isDestroyed()) {
			this.view.webContents.once("destroyed", onDestroyed);
		}

		importCookiesViaCdp(this.session, handle)
			.then((count) => {
				stopGuard();
				this.pending = false;
				cleanup();

				if (count === 0) {
					this.showError(
						"No Google cookies found. Make sure you completed sign-in in the Chrome window.",
					);
					return;
				}

				if (!this.view.webContents.isDestroyed()) {
					this.view.webContents.loadURL(continueUrl);
				}
			})
			.catch((error: Error) => {
				stopGuard();
				this.pending = false;
				cleanup();
				this.showError(error.message);
			});
	}

	private showError(message: string): void {
		if (this.view.webContents.isDestroyed()) {
			return;
		}

		this.view.webContents
			.executeJavaScript(`
			document.getElementById("btn").disabled = false;
			document.getElementById("btn").textContent = "Transfer session to Pane";
			var err = document.getElementById("err");
			err.style.display = "block";
			err.textContent = ${JSON.stringify(message)};
		`)
			.catch((error: Error) => {
				console.warn("[SignIn] Failed to show auth error:", error.message);
			});
	}
}
