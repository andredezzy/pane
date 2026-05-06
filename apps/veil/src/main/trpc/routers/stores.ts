import { z } from "zod/v4";
import type { StoreApi } from "zustand/vanilla";

import { serializeState } from "../../../stores/middlewares/serialize";
import { procedure, router } from "../trpc";

const StoreNameSchema = z.enum([
	"profile-store",
	"tab-store",
	"navigation-store",
	"settings-store",
	"extension-store",
	"security-store",
]);

export const storesRouter = router({
	push: procedure
		.input(z.object({ name: StoreNameSchema, state: z.string() }))
		.mutation(({ input, ctx }) => {
			const store = ctx.stores[input.name];
			const partial = JSON.parse(input.state);
			store.setState((prev) => ({ ...prev, ...partial }));
		}),

	sync: procedure
		.input(z.object({ name: StoreNameSchema }))
		.subscription(async function* ({ input, ctx }) {
			const store = ctx.stores[input.name];
			yield serializeState(store.getState());
			yield* storeChanges(store);
		}),
});

async function* storeChanges(store: StoreApi<object>) {
	let resolve: (() => void) | null = null;
	let pending = false;
	let timer: ReturnType<typeof setTimeout> | null = null;

	const unsubscribe = store.subscribe(() => {
		pending = true;

		if (timer) {
			clearTimeout(timer);
		}

		timer = setTimeout(() => {
			timer = null;

			if (resolve) {
				resolve();
				resolve = null;
			}
		}, 16);
	});

	try {
		while (true) {
			if (!pending) {
				await new Promise<void>((r) => {
					resolve = r;
				});
			}

			pending = false;
			yield serializeState(store.getState());
		}
	} finally {
		unsubscribe();

		if (timer) {
			clearTimeout(timer);
		}
	}
}
