import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import os from "node:os";
import http from "node:http";

const CHROME_PATHS = [
	"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
	"/Applications/Chromium.app/Contents/MacOS/Chromium",
	"/Applications/Google Chrome Canary.app/Contents/MacOS/Google Chrome Canary",
];

const CDP_PORT = 19222;

interface CdpCookie {
	name: string;
	value: string;
	domain: string;
	path: string;
	secure: boolean;
	httpOnly: boolean;
	expires: number;
	sameSite: string;
}

function findChrome(): string | null {
	for (const chromePath of CHROME_PATHS) {
		if (fs.existsSync(chromePath)) {
			return chromePath;
		}
	}

	return null;
}

export function launchChromeForGoogleAuth(continueUrl: string): boolean {
	const chromePath = findChrome();

	if (!chromePath) {
		return false;
	}

	const tempDir = path.join(os.tmpdir(), `pane-google-auth-${Date.now()}`);
	fs.mkdirSync(tempDir, { recursive: true });

	const loginUrl = `https://accounts.google.com/ServiceLogin?continue=${encodeURIComponent(continueUrl)}`;

	const child = spawn(chromePath, [
		`--user-data-dir=${tempDir}`,
		`--remote-debugging-port=${CDP_PORT}`,
		"--no-first-run",
		"--no-default-browser-check",
		loginUrl,
	], {
		detached: true,
		stdio: "ignore",
	});

	child.unref();

	return true;
}

export async function importCookiesViaCdp(
	targetSession: Electron.Session,
): Promise<number> {
	const wsUrl = await getPageWebSocketUrl();

	if (!wsUrl) {
		throw new Error("Could not connect to Chrome. Is the Chrome window still open?");
	}

	const allCookies = await fetchCookiesViaCdp(wsUrl);

	const googleCookies = allCookies.filter(
		(c) =>
			c.domain.includes("google.com") ||
			c.domain.includes("youtube.com"),
	);

	const sameSiteMap: Record<string, "unspecified" | "no_restriction" | "lax" | "strict"> = {
		None: "no_restriction",
		Lax: "lax",
		Strict: "strict",
	};

	let count = 0;

	for (const cookie of googleCookies) {
		const url = `http${cookie.secure ? "s" : ""}://${cookie.domain.replace(/^\./, "")}${cookie.path}`;

		try {
			await targetSession.cookies.set({
				url,
				name: cookie.name,
				value: cookie.value,
				domain: cookie.domain,
				path: cookie.path,
				secure: cookie.secure,
				httpOnly: cookie.httpOnly,
				expirationDate: cookie.expires > 0 ? cookie.expires : undefined,
				sameSite: sameSiteMap[cookie.sameSite] ?? "unspecified",
			});

			count++;
		} catch {}
	}

	return count;
}

async function getPageWebSocketUrl(): Promise<string | null> {
	for (let attempt = 0; attempt < 5; attempt++) {
		try {
			const data = await httpGet(`http://127.0.0.1:${CDP_PORT}/json/list`);
			const targets = JSON.parse(data);
			const page = targets.find((t: any) => t.type === "page");

			if (page?.webSocketDebuggerUrl) {
				return page.webSocketDebuggerUrl;
			}
		} catch {}

		await new Promise((r) => setTimeout(r, 1000));
	}

	return null;
}

function httpGet(url: string): Promise<string> {
	return new Promise((resolve, reject) => {
		http.get(url, (res) => {
			let data = "";

			res.on("data", (chunk) => (data += chunk));
			res.on("end", () => resolve(data));
		}).on("error", reject);
	});
}

function fetchCookiesViaCdp(wsUrl: string): Promise<CdpCookie[]> {
	return new Promise((resolve, reject) => {
		const WS = (require("ws") as any).default ?? require("ws");
		const ws = new WS(wsUrl);

		let nextId = 1;

		const timeout = setTimeout(() => {
			ws.close();
			reject(new Error("CDP timeout"));
		}, 10000);

		ws.on("open", () => {
			ws.send(JSON.stringify({ id: nextId++, method: "Network.enable" }));
		});

		ws.on("message", (data: Buffer) => {
			try {
				const msg = JSON.parse(data.toString());

				if (msg.id === 1 && !msg.error) {
					ws.send(JSON.stringify({ id: nextId++, method: "Network.getAllCookies" }));
					return;
				}

				if (msg.result?.cookies) {
					clearTimeout(timeout);
					ws.close();
					resolve(msg.result.cookies);
					return;
				}

				if (msg.error) {
					clearTimeout(timeout);
					ws.close();
					reject(new Error(msg.error.message));
				}
			} catch {}
		});

		ws.on("error", (error: Error) => {
			clearTimeout(timeout);
			reject(error);
		});
	});
}
