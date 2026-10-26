import { join, normalize } from "node:path";

const PAGES_DIR = join(import.meta.dir, "..", "pages");

export type Fixtures = {
	origin: string;
	url: (path: string) => string;
	stop: () => void;
};

const html = (title: string, body: string) =>
	new Response(
		`<!doctype html><html lang="en"><head><meta charset="utf-8"><title>${title}</title></head><body>${body}</body></html>`,
		{ headers: { "Content-Type": "text/html; charset=utf-8" } },
	);

// Pages that are not files: HTTP authentication (user "pane", password
// "secret"), a POST echo for form resubmission, and an IP echo.
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

	// The proxy test's IP echo, in httpbin's shape.
	if (pathname === "/ip") {
		return Response.json({ origin: "203.0.113.7" });
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
