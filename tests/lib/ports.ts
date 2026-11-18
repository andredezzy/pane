// The ports the runner's servers listen on: the same in every run, where
// they're free, since Pane shows them (a sign-in sheet's host, the profile
// sheet's proxy, the address field) and two runs' renders match (ADR 0128).
// Each server takes the first free port from its family's, counting up past
// one taken, by another of the run's servers or another app's; below the
// ports macOS and Windows hand out to connections (49152 on).
import type * as net from "node:net";

export const FIXTURES_PORT = 41_000;
// The proxies and servers a scenario starts itself, in the order it starts
// them.
export const SCENARIO_PORTS = 41_100;

const TRIES = 100;

function taken(error: unknown): boolean {
	return (error as { code?: string } | null)?.code === "EADDRINUSE";
}

// Bun.serve on the first free port from `first`, `serve` making it on one.
export function serveFrom<T>(first: number, serve: (port: number) => T): T {
	for (let port = first; port < first + TRIES; port++) {
		try {
			return serve(port);
		} catch (error) {
			if (!taken(error)) {
				throw error;
			}
		}
	}

	return serve(0);
}

// `server` listening on `host`'s first free port from `first`: the port.
export async function listenFrom(
	server: net.Server,
	host: string,
	first: number,
): Promise<number> {
	for (let port = first; port < first + TRIES; port++) {
		const listening = await new Promise<boolean>((done, fail) => {
			const failed = (error: Error) => {
				server.off("listening", listened);

				if (taken(error)) {
					done(false);
				} else {
					fail(error);
				}
			};

			const listened = () => {
				server.off("error", failed);
				done(true);
			};

			server.once("error", failed);
			server.once("listening", listened);
			server.listen(port, host);
		});

		if (listening) {
			return port;
		}
	}

	return await new Promise((done) => {
		server.listen(0, host, () => {
			done((server.address() as net.AddressInfo).port);
		});
	});
}
