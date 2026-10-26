import { join, normalize } from "node:path";

const PAGES_DIR = join(import.meta.dir, "..", "pages");

export type Fixtures = {
	origin: string;
	url: (path: string) => string;
	stop: () => void;
};

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
