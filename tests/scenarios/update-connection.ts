import { createHash, randomBytes } from "node:crypto";
import { readdirSync, readFileSync } from "node:fs";
import { createServer, type ServerResponse } from "node:http";
import type { AddressInfo } from "node:net";
import { join } from "node:path";
import { launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

// A download's connections (ADR 0002), against a stand-in release. One that
// crawls is left: where the server takes ranges the rest comes from where it
// got to, the bar never going back; where it offers none, or refuses them when
// asked, the file comes from the start, while that loses little. Far enough in,
// a connection that cannot resume stays, and so does one once its server has
// refused to resume: it is asked only once. One that drops goes on from where it
// got to. One of unknown length at a good rate is left alone, and so is one judged
// from its first bytes, however late they come. Each way the file
// matches the release's digest. A node server: it sends a stream's
// Content-Length, as GitHub does, where Bun.serve drops it.

const VERSION = "99.0.0";
const DMG = `Pane-${VERSION}-arm64.dmg`;
const MB = 1024 * 1024;
const VALIDATORS = { "Accept-Ranges": "bytes", ETag: '"pane-stand-in"' };

type Kind =
	| "resumes"
	| "noRanges"
	| "refuses"
	| "refusedOnce"
	| "stays"
	| "lateStart"
	| "drops"
	| "unknownLength";

type Case = {
	size: number;
	// The first connection's body.
	first: (payload: Buffer, response: ServerResponse) => void;
	validators: boolean;
	length: boolean;
	refusesRanges?: boolean;
	// Every connection without a range crawls too.
	allCrawl?: boolean;
};

// `chunk` bytes every 100 ms from `from` on, until the end or the client goes.
function trickle(
	payload: Buffer,
	response: ServerResponse,
	from: number,
	chunk: number,
) {
	let sent = from;

	const timer = setInterval(() => {
		response.write(payload.subarray(sent, sent + chunk));
		sent += chunk;

		if (sent >= payload.length) {
			clearInterval(timer);
			response.end();
		}
	}, 100);

	response.on("close", () => clearInterval(timer));
}

// 160 KB/s: a slow connection, not a dead one.
const SLOW = 16 * 1024;

const CASES: Record<Kind, Case> = {
	resumes: {
		size: 40 * MB,
		first: (payload, response) => trickle(payload, response, 0, SLOW),
		validators: true,
		length: true,
	},
	noRanges: {
		size: 40 * MB,
		first: (payload, response) => trickle(payload, response, 0, SLOW),
		validators: false,
		length: true,
	},
	refuses: {
		size: 40 * MB,
		first: (payload, response) => trickle(payload, response, 0, SLOW),
		validators: true,
		length: true,
		refusesRanges: true,
	},
	// Crawling from the start each time: refused once, it does not ask again.
	refusedOnce: {
		size: 4 * MB,
		first: (payload, response) => trickle(payload, response, 0, SLOW),
		validators: true,
		length: true,
		refusesRanges: true,
		allCrawl: true,
	},
	// Most of it fast, then the trickle past the next window: 15 s of it.
	stays: {
		size: 30 * MB + (5 * MB) / 2,
		first: (payload, response) => {
			response.write(payload.subarray(0, 30 * MB));
			trickle(payload, response, 30 * MB, SLOW);
		},
		validators: false,
		length: true,
	},
	// Nothing for 4.5 s, the setup of a slow network, then 2 MB/s.
	lateStart: {
		size: 14 * MB,
		first: (payload, response) => {
			setTimeout(() => trickle(payload, response, 0, MB / 5), 4_500);
		},
		validators: true,
		length: true,
	},
	// 8 MB, then the connection is cut.
	drops: {
		size: 40 * MB,
		first: (payload, response) => {
			response.write(payload.subarray(0, 8 * MB));
			setTimeout(() => response.destroy(), 300);
		},
		validators: true,
		length: true,
	},
	// 2 MB/s for 7 s, past the first window, with no length to say how far.
	unknownLength: {
		size: 14 * MB,
		first: (payload, response) => trickle(payload, response, 0, MB / 5),
		validators: false,
		length: false,
	},
};

async function serve(kind: Kind) {
	const { size, first, validators, length, refusesRanges, allCrawl } =
		CASES[kind];

	const payload = randomBytes(size);
	const sha256 = createHash("sha256").update(payload).digest("hex");
	const requests: { range: string | null }[] = [];

	const server = createServer((request, response) => {
		const { pathname } = new URL(request.url ?? "/", "http://stand-in");

		if (pathname === "/releases/latest") {
			const { port } = server.address() as AddressInfo;

			response.setHeader("Content-Type", "application/json");

			response.end(
				JSON.stringify({
					tag_name: `v${VERSION}`,
					published_at: "2026-11-04T00:00:00Z",
					assets: [
						{
							name: DMG,
							browser_download_url: `http://127.0.0.1:${port}/download/${DMG}`,
							digest: `sha256:${sha256}`,
						},
					],
				}),
			);

			return;
		}

		const range = request.headers.range ?? null;
		requests.push({ range });

		const headers = {
			"Content-Type": "application/octet-stream",
			...(validators && VALIDATORS),
		};

		if (requests.length === 1) {
			response.writeHead(200, {
				...headers,
				...(length && { "Content-Length": String(size) }),
			});

			first(payload, response);

			return;
		}

		if (range && refusesRanges) {
			response.writeHead(403);
			response.end("expired");

			return;
		}

		if (!range && allCrawl) {
			response.writeHead(200, { ...headers, "Content-Length": String(size) });
			trickle(payload, response, 0, SLOW);

			return;
		}

		const from = Number(range?.match(/bytes=(\d+)-/)?.[1] ?? 0);

		response.writeHead(range ? 206 : 200, {
			...headers,
			"Content-Length": String(size - from),
			...(range && {
				"Content-Range": `bytes ${from}-${size - 1}/${size}`,
			}),
		});

		response.end(payload.subarray(from));
	});

	await new Promise<void>((listening) =>
		server.listen(0, "127.0.0.1", listening),
	);

	const { port } = server.address() as AddressInfo;

	return { server, requests, sha256, origin: `http://127.0.0.1:${port}` };
}

const updateConnection: Scenario = async (context) => {
	const results: Record<string, unknown> = {};

	for (const kind of Object.keys(CASES) as Kind[]) {
		const { server, requests, sha256, origin } = await serve(kind);
		const downloads = context.tempDir("pane-connection-");

		try {
			const app = await launchPane(context, {
				env: {
					PANE_UPDATE_URL: `${origin}/releases/latest`,
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

			const bar: number[] = [];

			const done = await app.waitFor(
				"the update downloaded",
				(state) => {
					if (state.update.progress !== null) {
						bar.push(state.update.progress);
					}

					return state.update.status === "downloaded";
				},
				60_000,
			);

			const seconds = (Date.now() - started) / 1000;

			const offset = Number(
				requests[1]?.range?.match(/bytes=(\d+)-/)?.[1] ?? 0,
			);

			const wentBack = bar.some((value, at) => value < (bar[at - 1] ?? 0));
			const file = readFileSync(join(downloads, DMG));

			const whole =
				createHash("sha256").update(file).digest("hex") === sha256 &&
				readdirSync(downloads).length === 1;

			const got = JSON.stringify({ seconds, requests, wentBack });

			expect(
				done.update.status === "downloaded" && whole,
				`${kind}: ${done.update.status}, the file whole: ${whole}, ${got}`,
			);

			const expected: Record<Kind, boolean> = {
				resumes: requests.length === 2 && offset > 0 && !wentBack,
				noRanges:
					requests.length === 2 && requests.every(({ range }) => !range),
				refuses:
					requests.length === 3 && offset > 0 && requests[2]?.range === null,
				refusedOnce:
					requests.filter(({ range }) => range !== null).length === 1 &&
					requests.at(-1)?.range === null,
				stays: requests.length === 1 && seconds > 10,
				lateStart: requests.length === 1 && seconds > 10,
				drops: requests.length === 2 && offset > 0 && !wentBack,
				unknownLength: requests.length === 1 && seconds > 5,
			};

			expect(expected[kind], `${kind}: ${got}`);
			results[kind] = { seconds, requests, resumedAt: offset };
			await app.browser.close();
		} finally {
			server.closeAllConnections();
			server.close();
		}
	}

	return results;
};

export default updateConnection;
