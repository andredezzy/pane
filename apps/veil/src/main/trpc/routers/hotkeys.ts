import { HotkeyEvent } from "../../hotkey-emitter";
import { procedure, router } from "../trpc";

export { HotkeyEvent };

export const hotkeysRouter = router({
	events: procedure.subscription(async function* ({ ctx, signal }) {
		const queue: HotkeyEvent[] = [];
		let resolve: (() => void) | null = null;

		const handler = (event: HotkeyEvent) => {
			queue.push(event);

			if (resolve) {
				resolve();
				resolve = null;
			}
		};

		ctx.hotkeyEmitter.onHotkey(handler);

		try {
			while (!signal?.aborted) {
				if (queue.length === 0) {
					await new Promise<void>((r) => {
						resolve = r;
					});
				}

				while (queue.length > 0) {
					const event = queue.shift();

					if (event) {
						yield event;
					}
				}
			}
		} finally {
			ctx.hotkeyEmitter.offHotkey(handler);
		}
	}),
});
