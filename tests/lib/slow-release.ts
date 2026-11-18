import { readFileSync } from "node:fs";
import { createServer } from "node:http";
import type { AddressInfo } from "node:net";
import type { StandInDmg } from "./update-copies";

// A stand-in release whose dmg comes slowly, in 64 KB pieces 100 ms apart
// (640 KB/s), with its length, as GitHub sends one, so a download shows its
// progress for a while. A node server: Bun.serve drops a stream's
// Content-Length. `digest` is asked at each check, so a run can offer a dmg
// whose digest is another. `hold(share)` stops the next download once it has
// sent that share of the dmg, until the function it returns lets it go, so
// what shows its progress shows the same in every run (ADR 0128).
export async function serveSlowRelease(
	dmg: StandInDmg,
	digest: () => string = () => dmg.sha256,
): Promise<{
	releases: string;
	hold: (share: number) => () => void;
	stop: () => void;
}> {
	const payload = readFileSync(dmg.path);
	const piece = 64 * 1024;
	let nextHold: { bytes: number; released: Promise<void> } | null = null;

	const server = createServer((request, response) => {
		const { pathname } = new URL(request.url ?? "/", "http://stand-in");
		const { port } = server.address() as AddressInfo;

		if (pathname === "/releases/latest") {
			response.setHeader("Content-Type", "application/json");

			response.end(
				JSON.stringify({
					tag_name: `v${dmg.version}`,
					published_at: "2026-11-11T00:00:00Z",
					assets: [
						{
							name: dmg.name,
							browser_download_url: `http://127.0.0.1:${port}/download/${dmg.name}`,
							digest: `sha256:${digest()}`,
						},
					],
				}),
			);

			return;
		}

		if (pathname !== `/download/${dmg.name}`) {
			response.writeHead(404);
			response.end();

			return;
		}

		response.writeHead(200, {
			"Content-Type": "application/octet-stream",
			"Content-Length": String(payload.length),
		});

		const hold = nextHold;
		let closed = false;

		nextHold = null;

		response.on("close", () => {
			closed = true;
		});

		void (async () => {
			for (let sent = 0; sent < payload.length && !closed; ) {
				if (hold && sent === hold.bytes) {
					await hold.released;
				}

				const end = Math.min(
					sent + piece,
					hold && sent < hold.bytes ? hold.bytes : payload.length,
				);

				response.write(payload.subarray(sent, end));
				sent = end;
				await Bun.sleep(100);
			}

			response.end();
		})();
	});

	await new Promise<void>((listening) =>
		server.listen(0, "127.0.0.1", listening),
	);

	const { port } = server.address() as AddressInfo;

	return {
		releases: `http://127.0.0.1:${port}/releases/latest`,
		hold: (share) => {
			let release = () => {};

			const released = new Promise<void>((done) => {
				release = done;
			});

			const hold = { bytes: Math.ceil(payload.length * share), released };

			nextHold = hold;

			return () => {
				if (nextHold === hold) {
					nextHold = null;
				}

				release();
			};
		},
		stop: () => {
			server.closeAllConnections();
			server.close();
		},
	};
}
