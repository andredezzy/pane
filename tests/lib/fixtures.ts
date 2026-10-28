import * as net from "node:net";
import { join, normalize } from "node:path";

const PAGES_DIR = join(import.meta.dir, "..", "pages");

export type Fixtures = {
	origin: string;
	url: (path: string) => string;
	stop: () => void;
};

const ACCEPT_CH = [
	"Sec-CH-UA-Arch",
	"Sec-CH-UA-Bitness",
	"Sec-CH-UA-Form-Factors",
	"Sec-CH-UA-Full-Version",
	"Sec-CH-UA-Full-Version-List",
	"Sec-CH-UA-Model",
	"Sec-CH-UA-Platform-Version",
	"Sec-CH-UA-WoW64",
].join(", ");

const html = (title: string, body: string) =>
	new Response(
		`<!doctype html><html lang="en"><head><meta charset="utf-8"><title>${title}</title></head><body>${body}</body></html>`,
		{ headers: { "Content-Type": "text/html; charset=utf-8" } },
	);

// Pages that are not files: HTTP authentication (user "pane", password
// "secret"), a POST echo for form resubmission, an IP echo, a request header
// echo, and a sign-in for the sleep scenario.
async function route(
	request: Request,
	pathname: string,
): Promise<Response | null> {
	if (pathname === "/auth") {
		const expected = `Basic ${btoa("pane:secret")}`;

		return request.headers.get("Authorization") === expected
			? html("Signed in", "<h1>Signed in as pane</h1>")
			: new Response("Sign in required", {
					status: 401,
					headers: { "WWW-Authenticate": 'Basic realm="Pane fixtures"' },
				});
	}

	// A cookie for each attribute the engine reports (the cookies scenario).
	// 127.0.0.1 is a trustworthy origin, so Secure ones are kept over http.
	if (pathname === "/cookie-attributes") {
		const headers = new Headers({ "Content-Type": "text/html" });

		for (const cookie of [
			"pane_session=1; Path=/",
			"pane_lax=1; Path=/; SameSite=Lax; Max-Age=3600",
			"pane_strict=1; Path=/cookie-attributes; SameSite=Strict; HttpOnly",
			"pane_none=1; Path=/; SameSite=None; Secure",
		]) {
			headers.append("Set-Cookie", cookie);
		}

		return new Response("<!doctype html><title>Cookies</title><h1>Set</h1>", {
			headers,
		});
	}

	// The proxy test's IP echo, in httpbin's shape.
	if (pathname === "/ip") {
		return Response.json({ origin: "203.0.113.7" });
	}

	// A stand-in for GitHub's latest release (every launch checks it):
	// `?tag=v99.0.0` offers that version, else an old one.
	if (pathname === "/releases/latest") {
		const tag = new URL(request.url).searchParams.get("tag") ?? "v0.0.1";
		const version = tag.replace(/^v/, "");
		const downloads = await slowDownloads();

		return Response.json({
			tag_name: tag,
			published_at: "2026-10-26T12:00:00Z",
			assets: [`Pane-${version}-x64.dmg`, `Pane-${version}-arm64.dmg`].map(
				(name) => ({
					name,
					browser_download_url: `${downloads}/releases/download/${tag}/${name}`,
				}),
			),
		});
	}

	// A sign-in for the sleep scenario's Trim. The first visit sets a session
	// cookie, local storage, a Cache Storage entry, and cacheable bytes; a
	// later one (the tab waking) sets nothing, so it shows what survived.
	if (pathname === "/sign-in") {
		const returning = (request.headers.get("Cookie") ?? "").includes(
			"pane_session=",
		);

		return new Response(
			`<!doctype html><title>Signed in</title><h1>Signed in</h1><script>
			window.ready = (async () => {
				if (localStorage.getItem("pane-session")) {
					return false;
				}
				localStorage.setItem("pane-session", "kept");
				await fetch("cacheable?n=1");
				await fetch("cacheable?n=2");
				const cache = await caches.open("pane-trim");
				await cache.put("cacheable?n=3", new Response("x".repeat(65536)));
				return true;
			})();
			</script>`,
			{
				headers: {
					"Content-Type": "text/html; charset=utf-8",
					...(returning
						? {}
						: {
								"Set-Cookie": "pane_session=signed-in; Path=/; Max-Age=86400",
							}),
				},
			},
		);
	}

	// 256 KB the HTTP cache keeps for an hour.
	if (pathname === "/cacheable") {
		return new Response("p".repeat(256 * 1024), {
			headers: {
				"Content-Type": "text/plain",
				"Cache-Control": "public, max-age=3600",
			},
		});
	}

	// The fingerprint page asks for every UA Client Hint, so the requests
	// after it carry them.
	if (pathname === "/fingerprint.html") {
		return new Response(Bun.file(join(PAGES_DIR, "fingerprint.html")), {
			headers: {
				"Content-Type": "text/html; charset=utf-8",
				"Accept-CH": ACCEPT_CH,
				"Cache-Control": "no-store",
			},
		});
	}

	// The request's own headers, as JSON or, for a frame, as a page that
	// holds them in `window.headers`.
	if (pathname === "/headers") {
		const headers = Object.fromEntries(request.headers);
		const noStore = { "Cache-Control": "no-store", "Accept-CH": ACCEPT_CH };

		return new URL(request.url).searchParams.get("format") === "html"
			? new Response(
					`<!doctype html><title>Headers</title><script>window.headers = ${JSON.stringify(headers)};</script>`,
					{
						headers: { ...noStore, "Content-Type": "text/html; charset=utf-8" },
					},
				)
			: Response.json(headers, { headers: noStore });
	}

	if (pathname === "/echo" && request.method === "POST") {
		const form = await request.formData();
		const count = Number(form.get("count") ?? 0);

		return html(
			"Posted",
			`<h1 id="posted">Posted ${String(form.get("value") ?? "")}</h1><script>sessionStorage.posts = String(Number(sessionStorage.posts ?? 0) + 1)</script><p>${count}</p>`,
		);
	}

	return null;
}

// A release's dmg: 4 MB, slowly, with its Content-Length, as GitHub serves
// it (Bun.serve drops the length from a streamed body, and a download's
// progress needs it). Started on first use; one for the whole run.
let downloadsOrigin: Promise<string> | null = null;

function slowDownloads(): Promise<string> {
	downloadsOrigin ??= new Promise((ready) => {
		const chunk = Buffer.alloc(256 * 1024, 0x50);
		const size = 16 * chunk.length;

		const server = net.createServer((socket) => {
			socket.on("error", () => socket.destroy());

			socket.once("data", async () => {
				socket.write(
					`HTTP/1.1 200 OK\r\nContent-Type: application/x-apple-diskimage\r\nContent-Length: ${size}\r\nConnection: close\r\n\r\n`,
				);

				for (let index = 0; index < 16 && !socket.destroyed; index++) {
					socket.write(chunk);
					await Bun.sleep(250);
				}

				socket.end();
			});
		});

		server.unref();

		server.listen(0, "127.0.0.1", () => {
			ready(`http://127.0.0.1:${(server.address() as net.AddressInfo).port}`);
		});
	});

	return downloadsOrigin;
}

// Serves tests/pages on 127.0.0.1 so every behavior check runs offline.
export function serveFixtures(): Fixtures {
	const server = Bun.serve({
		hostname: "127.0.0.1",
		port: 0,
		// `?delay=<ms>` holds the response back (a slow load to stop);
		// `?download=1` serves the file as an attachment.
		async fetch(request) {
			const { pathname, searchParams } = new URL(request.url);
			const delay = Number(searchParams.get("delay") ?? 0);

			if (delay > 0) {
				await Bun.sleep(delay);
			}

			const routed = await route(request, pathname);

			if (routed) {
				return routed;
			}

			const relative = normalize(decodeURIComponent(pathname)).replace(
				/^(\.\.(\/|$))+/,
				"",
			);

			const file = Bun.file(
				join(PAGES_DIR, relative === "/" ? "index.html" : relative),
			);

			if (!(await file.exists())) {
				return new Response("not found", { status: 404 });
			}

			return searchParams.has("download")
				? new Response(file, {
						headers: {
							"Content-Disposition": `attachment; filename="${relative.split("/").pop()}"`,
						},
					})
				: new Response(file);
		},
	});

	const origin = `http://127.0.0.1:${server.port}`;

	return {
		origin,
		url: (path) => `${origin}/${path.replace(/^\//, "")}`,
		stop: () => server.stop(true),
	};
}
