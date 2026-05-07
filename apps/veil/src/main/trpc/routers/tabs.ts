import { z } from "zod/v4";
import type { FindResult } from "../../../constants/find-result";
import type { TabState } from "../../../stores/tab-store";
import type { Context } from "../trpc";
import { procedure, router } from "../trpc";

function findActiveProfile(ctx: Context) {
	const { activeProfileId } = ctx.stores["tab-store"].getState() as TabState;

	return activeProfileId ? ctx.pane.getProfile(activeProfileId) : undefined;
}

export const tabsRouter = router({
	open: procedure
		.input(z.object({ profileId: z.string(), url: z.string().optional() }))
		.mutation(({ input, ctx }) => {
			ctx.pane.hideAllTabs();
			ctx.pane.getOrCreateProfile(input.profileId).tabs.open(input.url);
		}),

	close: procedure
		.input(z.object({ tabId: z.string() }))
		.mutation(({ input, ctx }) => {
			ctx.pane.getProfileForTab(input.tabId)?.tabs.close(input.tabId);
		}),

	switch: procedure
		.input(z.object({ tabId: z.string() }))
		.mutation(({ input, ctx }) => {
			ctx.pane.hideAllTabs();
			ctx.pane.getProfileForTab(input.tabId)?.tabs.activate(input.tabId);
		}),

	navigate: procedure
		.input(z.object({ url: z.string() }))
		.mutation(({ input, ctx }) => {
			findActiveProfile(ctx)?.tabs.navigate(input.url);
		}),

	goBack: procedure.mutation(({ ctx }) => {
		findActiveProfile(ctx)?.tabs.goBack();
	}),

	goForward: procedure.mutation(({ ctx }) => {
		findActiveProfile(ctx)?.tabs.goForward();
	}),

	reload: procedure.mutation(({ ctx }) => {
		findActiveProfile(ctx)?.tabs.reload();
	}),

	stop: procedure.mutation(({ ctx }) => {
		findActiveProfile(ctx)?.tabs.stop();
	}),

	hideAll: procedure.mutation(({ ctx }) => {
		ctx.pane.hideAllTabs();
	}),

	showActive: procedure.mutation(({ ctx }) => {
		findActiveProfile(ctx)?.tabs.showActive();
	}),

	find: procedure
		.input(
			z.object({
				text: z.string(),
				forward: z.boolean().optional(),
				findNext: z.boolean().optional(),
			}),
		)
		.mutation(({ input, ctx }) => {
			findActiveProfile(ctx)?.tabs.find(input.text, {
				...(input.forward !== undefined && { forward: input.forward }),
				...(input.findNext !== undefined && { findNext: input.findNext }),
			});
		}),

	stopFind: procedure.mutation(({ ctx }) => {
		findActiveProfile(ctx)?.tabs.stopFind();
	}),

	findResults: procedure.subscription(async function* ({ ctx, signal }) {
		const queue: FindResult[] = [];
		let resolve: (() => void) | null = null;

		const handler = (result: FindResult) => {
			queue.push(result);

			if (resolve) {
				resolve();
				resolve = null;
			}
		};

		ctx.findEmitter.onResult(handler);

		try {
			while (!signal?.aborted) {
				if (queue.length === 0) {
					await new Promise<void>((r) => {
						resolve = r;
					});
				}

				while (queue.length > 0) {
					const result = queue.shift();

					if (result) {
						yield result;
					}
				}
			}
		} finally {
			ctx.findEmitter.offResult(handler);
		}
	}),
});
