// Minimal Chrome DevTools Protocol client over one browser-level WebSocket,
// using flattened sessions so every target shares the same connection.

type Pending = {
	resolve: (value: Record<string, unknown>) => void;
	reject: (error: Error) => void;
};

type EventMessage = {
	method: string;
	params: Record<string, unknown>;
	sessionId?: string;
};

type Listener = (message: EventMessage) => void;

// Message ids only need to be unique per socket; one counter covers every socket.
let nextMessageId = 1;

export class Cdp {
	private readonly pending = new Map<number, Pending>();
	private readonly listeners = new Set<Listener>();
	// Callbacks for the socket closing, so waiters fail at once.
	private readonly closers = new Set<(error: Error) => void>();
	// Set once the socket closed: the browser quit, crashed, or close() ran.
	private closedBy: Error | null = null;

	private constructor(private readonly socket: WebSocket) {
		socket.addEventListener("message", (event) => {
			const message = JSON.parse(String(event.data));

			if (typeof message.id === "number") {
				const waiter = this.pending.get(message.id);
				this.pending.delete(message.id);

				if (message.error) {
					waiter?.reject(
						new Error(`${message.error.message} (${message.error.code})`),
					);
				} else {
					waiter?.resolve(message.result ?? {});
				}

				return;
			}

			for (const listener of this.listeners) {
				listener(message);
			}
		});

		// A browser that quits never answers what is still pending: fail it
		// now rather than at each call's timeout.
		socket.addEventListener("close", () =>
			this.fail(new Error("the DevTools connection closed")),
		);

		socket.addEventListener("error", () =>
			this.fail(new Error("the DevTools connection failed")),
		);
	}

	private fail(error: Error): void {
		if (this.closedBy) {
			return;
		}

		this.closedBy = error;

		for (const waiter of this.pending.values()) {
			waiter.reject(error);
		}

		this.pending.clear();

		for (const closer of this.closers) {
			closer(error);
		}

		this.closers.clear();
	}

	get closed(): boolean {
		return this.closedBy !== null;
	}

	static connect(url: string, timeoutMs = 15_000): Promise<Cdp> {
		return new Promise((resolve, reject) => {
			const socket = new WebSocket(url);

			const timer = setTimeout(() => {
				socket.close();
				reject(new Error(`no connection to ${url} after ${timeoutMs} ms`));
			}, timeoutMs);

			socket.addEventListener("open", () => {
				clearTimeout(timer);
				resolve(new Cdp(socket));
			});

			socket.addEventListener("error", () => {
				clearTimeout(timer);
				reject(new Error(`cannot connect to ${url}`));
			});
		});
	}

	// Rejects after `timeoutMs`: a target whose renderer died never answers.
	send<T = Record<string, unknown>>(
		method: string,
		params: Record<string, unknown> = {},
		sessionId?: string,
		timeoutMs = 15_000,
	): Promise<T> {
		if (this.closedBy) {
			return Promise.reject(new Error(`${method}: ${this.closedBy.message}`));
		}

		const id = nextMessageId++;
		this.socket.send(JSON.stringify({ id, method, params, sessionId }));

		return new Promise((resolve, reject) => {
			const timer = setTimeout(() => {
				this.pending.delete(id);
				reject(new Error(`${method} timed out after ${timeoutMs} ms`));
			}, timeoutMs);

			this.pending.set(id, {
				resolve: (value) => {
					clearTimeout(timer);
					resolve(value as T);
				},
				reject: (error) => {
					clearTimeout(timer);
					reject(error);
				},
			});
		});
	}

	on(listener: Listener): () => void {
		this.listeners.add(listener);

		return () => this.listeners.delete(listener);
	}

	waitFor(
		method: string,
		predicate: (message: EventMessage) => boolean = () => true,
		timeoutMs = 15_000,
	): Promise<EventMessage> {
		if (this.closedBy) {
			return Promise.reject(
				new Error(`waiting for ${method}: ${this.closedBy.message}`),
			);
		}

		return new Promise((resolve, reject) => {
			const done = () => {
				clearTimeout(timer);
				off();
				this.closers.delete(closed);
			};

			const timer = setTimeout(() => {
				done();
				reject(new Error(`timed out waiting for ${method}`));
			}, timeoutMs);

			const closed = (error: Error) => {
				done();
				reject(new Error(`waiting for ${method}: ${error.message}`));
			};

			const off = this.on((message) => {
				if (message.method === method && predicate(message)) {
					done();
					resolve(message);
				}
			});

			this.closers.add(closed);
		});
	}

	// Safe to call more than once.
	close(): void {
		this.fail(new Error("the DevTools connection was closed"));
		this.socket.close();
	}
}

export class Page {
	constructor(
		readonly cdp: Cdp,
		readonly sessionId: string,
		readonly targetId: string,
	) {}

	send<T = Record<string, unknown>>(
		method: string,
		params: Record<string, unknown> = {},
	): Promise<T> {
		return this.cdp.send<T>(method, params, this.sessionId);
	}

	async evaluate<T = unknown>(expression: string): Promise<T> {
		const { result, exceptionDetails } = await this.send<{
			result: { value?: T };
			exceptionDetails?: { text: string; exception?: { description?: string } };
		}>("Runtime.evaluate", {
			expression,
			returnByValue: true,
			awaitPromise: true,
		});

		if (exceptionDetails) {
			throw new Error(
				exceptionDetails.exception?.description ?? exceptionDetails.text,
			);
		}

		return result.value as T;
	}

	async navigate(url: string): Promise<void> {
		const loaded = this.cdp.waitFor(
			"Page.loadEventFired",
			(message) => message.sessionId === this.sessionId,
		);

		// Awaited below; a navigation that fails first must not leave it
		// rejected with no handler.
		loaded.catch(() => {});
		await this.send("Page.navigate", { url });
		await loaded;
	}
}

// Attaches to an existing page target, or creates one when no id is given.
export async function openPage(
	cdp: Cdp,
	url = "about:blank",
	targetId?: string,
): Promise<Page> {
	const id =
		targetId ??
		(
			await cdp.send<{ targetId: string }>("Target.createTarget", {
				url: "about:blank",
			})
		).targetId;

	const { sessionId } = await cdp.send<{ sessionId: string }>(
		"Target.attachToTarget",
		{ targetId: id, flatten: true },
	);

	const page = new Page(cdp, sessionId, id);

	await page.send("Page.enable");
	await page.send("Runtime.enable");

	if (url !== "about:blank") {
		await page.navigate(url);
	}

	return page;
}
