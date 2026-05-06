import type { AnyTRPCRouter, inferRouterContext } from "@trpc/server";
import {
	callTRPCProcedure,
	getErrorShape,
	getTRPCErrorFromUnknown,
	isTrackedEnvelope,
	TRPCError,
	transformTRPCResponse,
} from "@trpc/server";
import {
	isObservable,
	observableToAsyncIterable,
} from "@trpc/server/observable";
import type { TRPCResponseMessage, TRPCResultMessage } from "@trpc/server/rpc";
import type { IpcMainEvent, WebContents } from "electron";
import { ipcMain } from "electron";

// Must match the channel used by trpc-electron's exposeElectronTRPC preload
const ELECTRON_TRPC_CHANNEL = "trpc-electron";

type ETRPCRequest =
	| {
			method: "request";
			operation: {
				id: number;
				type: "query" | "mutation" | "subscription";
				path: string;
				input?: unknown;
			};
	  }
	| { method: "subscription.stop"; id: number };

function isAsyncIterable<T>(value: unknown): value is AsyncIterable<T> {
	return (
		typeof Symbol === "function" &&
		!!Symbol.asyncIterator &&
		!!value &&
		typeof value === "object" &&
		!Array.isArray(value) &&
		Symbol.asyncIterator in value
	);
}

function getInternalId(event: IpcMainEvent, request: ETRPCRequest): string {
	const messageId =
		request.method === "request" ? request.operation.id : request.id;

	return `${event.sender.id}-${event.senderFrame?.routingId ?? 0}:${messageId}`;
}

interface IPCHandlerOptions<TRouter extends AnyTRPCRouter> {
	router: TRouter;
	webContents: WebContents;
	createContext: () => inferRouterContext<TRouter>;
}

export function createIPCHandler<TRouter extends AnyTRPCRouter>({
	router,
	webContents,
	createContext,
}: IPCHandlerOptions<TRouter>): () => void {
	const subscriptions = new Map<string, AbortController>();

	const webContentsId = webContents.id;

	function cleanUpSubscriptions(frameRoutingId?: number) {
		const prefix = `${webContentsId}-${frameRoutingId ?? ""}`;
		for (const [key, sub] of subscriptions.entries()) {
			if (key.startsWith(prefix)) {
				sub.abort();
				subscriptions.delete(key);
			}
		}
	}

	function respond(event: IpcMainEvent, response: TRPCResponseMessage) {
		if (event.sender.isDestroyed()) {
			return;
		}

		event.reply(
			ELECTRON_TRPC_CHANNEL,
			transformTRPCResponse(router._def._config, response),
		);
	}

	async function handleMessage(event: IpcMainEvent, message: ETRPCRequest) {
		const internalId = getInternalId(event, message);

		if (message.method === "subscription.stop") {
			subscriptions.get(internalId)?.abort();

			return;
		}

		const { type, input: serializedInput, path, id } = message.operation;

		const input = serializedInput
			? router._def._config.transformer.input.deserialize(serializedInput)
			: undefined;

		const ctx = createContext();

		try {
			const abortController = new AbortController();

			const result = await callTRPCProcedure({
				router,
				ctx,
				path,
				getRawInput: async () => input,
				type,
				signal: abortController.signal,
				batchIndex: 0,
			});

			const isIterableResult = isAsyncIterable(result) || isObservable(result);

			if (type !== "subscription") {
				if (isIterableResult) {
					throw new TRPCError({
						code: "UNSUPPORTED_MEDIA_TYPE",
						message: `Cannot return an async iterable or observable from a ${type} procedure.`,
					});
				}

				respond(event, {
					id,
					result: { type: "data", data: result },
				});

				return;
			}

			if (!isIterableResult) {
				throw new TRPCError({
					message: `Subscription ${path} did not return an observable or AsyncGenerator`,
					code: "INTERNAL_SERVER_ERROR",
				});
			}

			if (subscriptions.has(internalId)) {
				throw new TRPCError({
					message: `Duplicate id ${internalId}`,
					code: "BAD_REQUEST",
				});
			}

			const iterable = isObservable(result)
				? observableToAsyncIterable(result, abortController.signal)
				: result;

			const iterator = iterable[Symbol.asyncIterator]();

			(async () => {
				try {
					const abortPromise = new Promise<"abort">((resolve) => {
						abortController.signal.onabort = () => resolve("abort");
					});

					while (true) {
						const next = await Promise.race([
							iterator.next().catch(getTRPCErrorFromUnknown),
							abortPromise,
						]);

						if (next === "abort") {
							await iterator.return?.();

							break;
						}

						if (next instanceof Error) {
							const error = getTRPCErrorFromUnknown(next);

							respond(event, {
								id,
								error: getErrorShape({
									config: router._def._config,
									error,
									type,
									path,
									input,
									ctx,
								}),
							});

							break;
						}

						if (next.done) {
							break;
						}

						let result: TRPCResultMessage<unknown>["result"] = {
							type: "data",
							data: next.value,
						};

						if (isTrackedEnvelope(next.value)) {
							const [trackId, data] = next.value;

							result = {
								type: "data",
								id: trackId,
								data: { id: trackId, data },
							};
						}

						respond(event, { id, result });
					}

					respond(event, { id, result: { type: "stopped" } });
					subscriptions.delete(internalId);
				} catch (cause) {
					const error = getTRPCErrorFromUnknown(cause);

					respond(event, {
						id,
						error: getErrorShape({
							config: router._def._config,
							error,
							type,
							path,
							input,
							ctx,
						}),
					});

					abortController.abort();
				}
			})();

			respond(event, { id, result: { type: "started" } });
			subscriptions.set(internalId, abortController);
		} catch (cause) {
			const error = getTRPCErrorFromUnknown(cause);

			respond(event, {
				id,
				error: getErrorShape({
					config: router._def._config,
					error,
					type,
					path,
					input,
					ctx,
				}),
			});
		}
	}

	const listener = (event: IpcMainEvent, request: ETRPCRequest) => {
		if (event.sender.id !== webContentsId) {
			return;
		}

		handleMessage(event, request);
	};

	ipcMain.on(ELECTRON_TRPC_CHANNEL, listener);

	webContents.on("did-start-navigation", ({ isSameDocument, frame }) => {
		if (!isSameDocument && frame) {
			cleanUpSubscriptions(frame.routingId);
		}
	});

	webContents.on("destroyed", () => {
		cleanUpSubscriptions();
		ipcMain.removeListener(ELECTRON_TRPC_CHANNEL, listener);
	});

	return () => {
		cleanUpSubscriptions();
		ipcMain.removeListener(ELECTRON_TRPC_CHANNEL, listener);
	};
}
