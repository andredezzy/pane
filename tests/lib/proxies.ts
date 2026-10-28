// Local proxies for the proxy scenario: an HTTP proxy with Basic auth (plain
// requests and CONNECT tunnels) and a SOCKS5 proxy with username and password
// (RFC 1929). Each logs what it carried and for whom. Hosts under `.pane.test`
// go to the fixture server, so a page can only be reached through a proxy;
// other hosts are connected for real.
import * as net from "node:net";

export type ProxyLogEntry = {
	proxy: "http" | "socks5";
	user: string | null;
	method: string;
	target: string;
};

export type TestProxy = {
	port: number;
	log: ProxyLogEntry[];
	stop: () => void;
};

const TEST_DOMAIN = ".pane.test";

function resolve(host: string, port: number, fixturePort: number) {
	return host.endsWith(TEST_DOMAIN)
		? { host: "127.0.0.1", port: fixturePort }
		: { host, port };
}

function listen(server: net.Server): Promise<number> {
	return new Promise((done) => {
		server.listen(0, "127.0.0.1", () => {
			done((server.address() as net.AddressInfo).port);
		});
	});
}

// HTTP proxy: answers 407 until the request carries the credentials.
export async function startHttpProxy(
	fixturePort: number,
	credentials: { username: string; password: string },
): Promise<TestProxy> {
	const log: ProxyLogEntry[] = [];
	const expected = `Basic ${btoa(`${credentials.username}:${credentials.password}`)}`;

	const server = net.createServer((client) => {
		client.on("error", () => client.destroy());
		let buffered = Buffer.alloc(0);

		const onData = (chunk: Buffer) => {
			buffered = Buffer.concat([buffered, chunk]);
			const end = buffered.indexOf("\r\n\r\n");

			if (end === -1) {
				return;
			}

			client.off("data", onData);
			const head = buffered.subarray(0, end).toString("latin1");
			const rest = buffered.subarray(end + 4);
			const [requestLine = "", ...headerLines] = head.split("\r\n");
			const [method = "", target = ""] = requestLine.split(" ");

			const authorization = headerLines
				.find((line) => line.toLowerCase().startsWith("proxy-authorization:"))
				?.slice("proxy-authorization:".length)
				.trim();

			if (authorization !== expected) {
				client.end(
					'HTTP/1.1 407 Proxy Authentication Required\r\nProxy-Authenticate: Basic realm="Pane test proxy"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n',
				);

				return;
			}

			log.push({ proxy: "http", user: credentials.username, method, target });

			if (method === "CONNECT") {
				const [host = "", port = "443"] = target.split(":");
				const destination = resolve(host, Number(port), fixturePort);

				const remote = net.connect(destination.port, destination.host, () => {
					client.write("HTTP/1.1 200 Connection Established\r\n\r\n");
					remote.write(rest);
					client.pipe(remote);
					remote.pipe(client);
				});

				remote.on("error", () => client.destroy());

				return;
			}

			// An absolute URI: forward the request with a relative path.
			const url = new URL(target);

			const destination = resolve(
				url.hostname,
				Number(url.port || 80),
				fixturePort,
			);

			const headers = headerLines.filter(
				(line) => !line.toLowerCase().startsWith("proxy-"),
			);

			const remote = net.connect(destination.port, destination.host, () => {
				remote.write(
					`${method} ${url.pathname}${url.search} HTTP/1.1\r\n${headers.join("\r\n")}\r\nConnection: close\r\n\r\n`,
				);

				remote.write(rest);
				client.pipe(remote);
				remote.pipe(client);
			});

			remote.on("error", () => client.destroy());
		};

		client.on("data", onData);
	});

	const port = await listen(server);

	return { port, log, stop: () => server.close() };
}

// SOCKS5 proxy: username and password only, CONNECT only.
export async function startSocks5Proxy(
	fixturePort: number,
	credentials: { username: string; password: string },
): Promise<TestProxy> {
	const log: ProxyLogEntry[] = [];

	const server = net.createServer((client) => {
		client.on("error", () => client.destroy());
		let buffered = Buffer.alloc(0);
		let stage: "greeting" | "auth" | "request" = "greeting";
		let user: string | null = null;

		const take = (count: number) => {
			if (buffered.length < count) {
				return null;
			}

			const taken = buffered.subarray(0, count);
			buffered = buffered.subarray(count);

			return taken;
		};

		const onData = (chunk: Buffer) => {
			buffered = Buffer.concat([buffered, chunk]);

			for (;;) {
				if (stage === "greeting") {
					if (buffered.length < 2 || buffered.length < 2 + (buffered[1] ?? 0)) {
						return;
					}

					const methods = take(2 + (buffered[1] ?? 0));

					if (!methods?.subarray(2).includes(0x02)) {
						client.end(Buffer.from([0x05, 0xff]));

						return;
					}

					client.write(Buffer.from([0x05, 0x02]));
					stage = "auth";
				} else if (stage === "auth") {
					if (buffered.length < 2) {
						return;
					}

					const userLength = buffered[1] ?? 0;

					if (buffered.length < 3 + userLength) {
						return;
					}

					const passwordLength = buffered[2 + userLength] ?? 0;
					const message = take(3 + userLength + passwordLength);

					if (!message) {
						return;
					}

					const name = message.subarray(2, 2 + userLength).toString();
					const password = message.subarray(3 + userLength).toString();

					const accepted =
						name === credentials.username && password === credentials.password;

					client.write(Buffer.from([0x01, accepted ? 0x00 : 0x01]));

					if (!accepted) {
						client.end();

						return;
					}

					user = name;
					stage = "request";
				} else {
					if (buffered.length < 5) {
						return;
					}

					const type = buffered[3] ?? 0;
					const addressLength = socksAddressLength(type, buffered[4] ?? 0);
					const request = take(4 + addressLength + 2);

					if (!request) {
						return;
					}

					client.off("data", onData);
					const host = socksHost(type, request.subarray(4, 4 + addressLength));

					const port = request.readUInt16BE(4 + addressLength);

					log.push({
						proxy: "socks5",
						user,
						method: "CONNECT",
						target: `${host}:${port}`,
					});

					const destination = resolve(host, port, fixturePort);

					const remote = net.connect(destination.port, destination.host, () => {
						client.write(
							Buffer.from([0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0]),
						);

						remote.write(buffered);
						client.pipe(remote);
						remote.pipe(client);
					});

					remote.on("error", () => {
						client.end(Buffer.from([0x05, 0x01, 0x00, 0x01, 0, 0, 0, 0, 0, 0]));
					});

					return;
				}
			}
		};

		client.on("data", onData);
	});

	const port = await listen(server);

	return { port, log, stop: () => server.close() };
}

// A SOCKS5 address's length: IPv4, IPv6, or a name after its length byte.
function socksAddressLength(type: number, nameLength: number): number {
	if (type === 0x01) {
		return 4;
	}

	if (type === 0x04) {
		return 16;
	}

	return 1 + nameLength;
}

function socksHost(type: number, address: Buffer): string {
	if (type === 0x01) {
		return [...address].join(".");
	}

	if (type === 0x03) {
		return address.subarray(1).toString();
	}

	return "ipv6";
}
