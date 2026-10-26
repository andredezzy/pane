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
		async fetch(request) {
			const { pathname } = new URL(request.url);

			const relative = normalize(decodeURIComponent(pathname)).replace(
				/^(\.\.(\/|$))+/,
				"",
			);

			const file = Bun.file(
				join(PAGES_DIR, relative === "/" ? "index.html" : relative),
			);

			return (await file.exists())
				? new Response(file)
				: new Response("not found", { status: 404 });
		},
	});

	const origin = `http://127.0.0.1:${server.port}`;

	return {
		origin,
		url: (path) => `${origin}/${path.replace(/^\//, "")}`,
		stop: () => server.stop(true),
	};
}
