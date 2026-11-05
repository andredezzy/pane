import { createHash, randomBytes } from "node:crypto";
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

// A download leaves a connection that crawls (ADR 0002), against a stand-in
// release whose first connection trickles and whose next ones run at full
// speed. One that takes ranges gets the rest from where it got to, the bar
// never going back. One that offers no ranges, or refuses them when asked,
// gets the file from the start. Each way the file matches the release's digest.

const VERSION = "99.0.0";
const DMG = `Pane-${VERSION}-arm64.dmg`;
const SIZE = 12 * 1024 * 1024;
const ETAG = '"pane-stand-in"';

type Server = "resumes" | "noRanges" | "refuses";

const payload = randomBytes(SIZE);
const sha256 = createHash("sha256").update(payload).digest("hex");

// 16 KB every 100 ms, 160 KB/s: a slow connection, not a dead one.
function trickle(): ReadableStream<Uint8Array> {
	let sent = 0;

	return new ReadableStream({
		async pull(controller) {
			await Bun.sleep(100);
			const chunk = payload.subarray(sent, sent + 16 * 1024);
			sent += chunk.length;
			controller.enqueue(new Uint8Array(chunk));

			if (sent >= SIZE) {
				controller.close();
			}
		},
	});
}

function serve(kind: Server) {
	const requests: { range: string | null }[] = [];

	const server = Bun.serve({
		hostname: "127.0.0.1",
		port: 0,
		idleTimeout: 60,
		fetch(request): Response {
			const { pathname, origin } = new URL(request.url);

			if (pathname === "/releases/latest") {
				return Response.json({
					tag_name: `v${VERSION}`,
					published_at: "2026-11-04T00:00:00Z",
					assets: [
						{
							name: DMG,
							browser_download_url: `${origin}/download/${DMG}`,
							digest: `sha256:${sha256}`,
						},
					],
				});
			}

			const range = request.headers.get("range");
			requests.push({ range });

			const validators: Record<string, string> =
				kind === "noRanges" ? {} : { "Accept-Ranges": "bytes", ETag: ETAG };

			const headers = {
				"Content-Type": "application/octet-stream",
				...validators,
			};

			if (requests.length === 1) {
				return new Response(trickle(), {
					headers: { ...headers, "Content-Length": String(SIZE) },
				});
			}

			if (range && kind === "refuses") {
				return new Response("expired", { status: 403 });
			}

			const from = Number(range?.match(/bytes=(\d+)-/)?.[1] ?? 0);

			return new Response(new Uint8Array(payload.subarray(from)), {
				status: range ? 206 : 200,
				headers: {
					...headers,
					"Content-Length": String(SIZE - from),
					...(range && {
						"Content-Range": `bytes ${from}-${SIZE - 1}/${SIZE}`,
					}),
				},
			});
		},
	});

	return { server, requests };
}

const updateConnection: Scenario = async (context) => {
	const results: Record<string, unknown> = {};

	for (const kind of ["resumes", "noRanges", "refuses"] as const) {
		const { server, requests } = serve(kind);
		const downloads = context.tempDir("pane-connection-");

		try {
			const app = await launchPane(context, {
				env: {
					PANE_UPDATE_URL: `${server.url.origin}/releases/latest`,
					PANE_UPDATE_DOWNLOADS: downloads,
					PANE_UPDATE_OPEN: "0",
					PANE_UPDATE_INSTALL: "0",
				},
			});

			await app.waitFor(
				"the update on offer",
				(state) => state.update.status === "available",
				15_000,
			);

			const started = Date.now();
			await app.send("downloadUpdate");

			// The bar as it goes.
			const bar: number[] = [];

			const done = await app.waitFor(
				"the update downloaded",
				(state) => {
					if (state.update.progress !== null) {
						bar.push(state.update.progress);
					}

					return state.update.status === "downloaded";
				},
				40_000,
			);

			const seconds = (Date.now() - started) / 1000;

			const offset = Number(
				requests[1]?.range?.match(/bytes=(\d+)-/)?.[1] ?? 0,
			);

			const file = readFileSync(join(downloads, DMG));

			const whole =
				createHash("sha256").update(file).digest("hex") === sha256 &&
				readdirSync(downloads).length === 1;

			expect(
				done.update.status === "downloaded" && whole,
				`${kind}: downloaded ${done.update.status}, the file whole: ${whole}`,
			);

			// The trickle alone would take over a minute.
			expect(seconds < 30, `${kind}: took ${seconds} s`);

			if (kind === "resumes") {
				const wentBack = bar.some((value, at) => value < (bar[at - 1] ?? 0));

				expect(
					requests.length === 2 && offset > 0 && !wentBack,
					`resumes: requests ${JSON.stringify(requests)}, bar ${JSON.stringify(bar)}`,
				);
			}

			if (kind === "noRanges") {
				expect(
					requests.length === 2 && requests.every(({ range }) => !range),
					`no ranges: requests ${JSON.stringify(requests)}`,
				);
			}

			if (kind === "refuses") {
				expect(
					requests.length === 3 && offset > 0 && requests[2]?.range === null,
					`refuses: requests ${JSON.stringify(requests)}`,
				);
			}

			results[kind] = { seconds, requests, resumedAt: offset };
			await app.browser.close();
		} finally {
			server.stop(true);
		}
	}

	return results;
};

export default updateConnection;
