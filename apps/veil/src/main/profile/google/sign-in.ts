import type { Session, WebContentsView } from "electron";
import { GOOGLE_AUTH_PAGE } from "./auth-page";
import {
	cleanupAuthChrome,
	importCookiesViaCdp,
	launchChromeForGoogleAuth,
} from "./auth-window";

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
		if (
			this.pending ||
			!url.includes("accounts.google.com") ||
			!url.includes("/rejected")
		) {
			return false;
		}

		this.pending = true;

		const continueUrl =
			new URL(url).searchParams.get("continue") || "https://www.google.com/";

		if (!launchChromeForGoogleAuth(continueUrl)) {
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
			if (message !== "__PANE_TRANSFER__") {
				return;
			}

			this.view.webContents.removeListener("console-message", onConsoleMessage);
			this.transfer(continueUrl);
		};

		this.view.webContents.on("console-message", onConsoleMessage);

		return true;
	}

	private transfer(continueUrl: string): void {
		importCookiesViaCdp(this.session)
			.then((count) => {
				this.pending = false;
				cleanupAuthChrome();

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
			err.textContent = "${message.replace(/"/g, '\\"')}";
		`)
			.catch(() => {});
	}
}
