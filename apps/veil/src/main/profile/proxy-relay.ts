import * as net from "node:net";
import { SocksClient } from "socks";

import { type ProxyConfig, ProxyType } from "../../stores/profile-store";

const SOCKS_TYPE_MAP = {
	[ProxyType.SOCKS4]: 4,
	[ProxyType.SOCKS5]: 5,
} as const;

export class ProxyRelay {
	private server: net.Server | null = null;
	private localPort = 0;

	constructor(private readonly config: ProxyConfig) {}

	get port(): number {
		return this.localPort;
	}

	get proxyUrl(): string {
		const scheme = this.needsRelay
			? "socks5"
			: this.config.proxyType.toLowerCase();

		const host = this.needsRelay ? "127.0.0.1" : this.config.host;
		const port = this.needsRelay ? this.localPort : this.config.port;

		return `${scheme}://${host}:${port}`;
	}

	get needsRelay(): boolean {
		const isSocks =
			this.config.proxyType === ProxyType.SOCKS4 ||
			this.config.proxyType === ProxyType.SOCKS5;

		return isSocks && Boolean(this.config.username);
	}

	async start(): Promise<void> {
		if (!this.needsRelay) {
			return;
		}

		const socksType =
			SOCKS_TYPE_MAP[
				this.config.proxyType as ProxyType.SOCKS4 | ProxyType.SOCKS5
			];

		const server = net.createServer((clientSocket) => {
			clientSocket.once("data", (firstChunk) => {
				this.handleSocks5Handshake(clientSocket, firstChunk, socksType);
			});

			clientSocket.on("error", () => clientSocket.destroy());
		});

		this.server = server;

		// A listen failure (EADDRINUSE, etc.) surfaces as an "error" event rather than
		// the listen callback — reject so proxyReady settles instead of hanging every
		// navigation forever. A post-listen error is logged, not swallowed, and must
		// not crash the process (the handler stays attached).
		await new Promise<void>((resolve, reject) => {
			let listening = false;

			server.on("error", (error) => {
				if (listening) {
					console.error("[ProxyRelay] server error after listen:", error);

					return;
				}

				this.server = null;
				reject(error);
			});

			server.listen(0, "127.0.0.1", () => {
				listening = true;
				this.localPort = (server.address() as net.AddressInfo).port;
				resolve();
			});
		});
	}

	stop(): void {
		this.server?.close();
		this.server = null;
	}

	private handleSocks5Handshake(
		clientSocket: net.Socket,
		firstChunk: Buffer,
		socksType: 4 | 5,
	): void {
		if (firstChunk[0] !== 0x05) {
			clientSocket.destroy();

			return;
		}

		clientSocket.write(Buffer.from([0x05, 0x00]));

		clientSocket.once("data", (requestChunk) => {
			this.handleSocks5Request(clientSocket, requestChunk, socksType);
		});
	}

	private handleSocks5Request(
		clientSocket: net.Socket,
		requestChunk: Buffer,
		socksType: 4 | 5,
	): void {
		const parsed = parseSocks5Request(requestChunk);

		if (!parsed) {
			clientSocket.destroy();

			return;
		}

		SocksClient.createConnection({
			command: "connect",
			destination: { host: parsed.host, port: parsed.port },
			proxy: {
				host: this.config.host,
				port: this.config.port,
				type: socksType,
				userId: this.config.username ?? undefined,
				password: this.config.password ?? undefined,
			},
			timeout: 15_000,
		})
			.then(({ socket: remoteSocket }) => {
				const reply = Buffer.from([
					0x05, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				]);

				clientSocket.write(reply);

				clientSocket.pipe(remoteSocket);
				remoteSocket.pipe(clientSocket);

				clientSocket.on("error", () => remoteSocket.destroy());
				remoteSocket.on("error", () => clientSocket.destroy());

				clientSocket.on("close", () => remoteSocket.destroy());
				remoteSocket.on("close", () => clientSocket.destroy());
			})
			.catch(() => {
				const errorReply = Buffer.from([
					0x05, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				]);

				clientSocket.write(errorReply);
				clientSocket.destroy();
			});
	}
}

function parseSocks5Request(
	data: Buffer,
): { host: string; port: number } | null {
	if (data.length < 7 || data[0] !== 0x05 || data[1] !== 0x01) {
		return null;
	}

	const addressType = data[3];
	let host: string;
	let portOffset: number;

	if (addressType === 0x01) {
		if (data.length < 10) return null;

		host = `${data[4]}.${data[5]}.${data[6]}.${data[7]}`;
		portOffset = 8;
	} else if (addressType === 0x03) {
		const hostnameLength = data[4];

		if (data.length < 5 + hostnameLength + 2) return null;

		host = data.subarray(5, 5 + hostnameLength).toString("ascii");
		portOffset = 5 + hostnameLength;
	} else if (addressType === 0x04) {
		if (data.length < 22) return null;

		const parts: string[] = [];
		for (let i = 0; i < 16; i += 2) {
			parts.push(data.readUInt16BE(4 + i).toString(16));
		}
		host = parts.join(":");
		portOffset = 20;
	} else {
		return null;
	}

	const port = data.readUInt16BE(portOffset);

	return { host, port };
}
