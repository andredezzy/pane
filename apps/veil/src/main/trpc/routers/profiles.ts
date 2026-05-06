import { z } from "zod/v4";

import { type TabState } from "../../../stores/tab-store";
import { procedure, router } from "../trpc";

export const profilesRouter = router({
	load: procedure
		.input(z.object({ profileId: z.string() }))
		.mutation(({ input, ctx }) => {
			ctx.pane.getOrCreateProfile(input.profileId).extensions.ensureLoaded();
		}),

	unload: procedure
		.input(z.object({ profileId: z.string() }))
		.mutation(({ input, ctx }) => {
			const profile = ctx.pane.getProfile(input.profileId);

			if (!profile) {
				return;
			}

			const tabState = ctx.stores["tab-store"].getState() as TabState;

			if (tabState.activeProfileId === input.profileId) {
				tabState.setActiveTab(null, null);
			}

			profile.tabs.unloadAll();
		}),

	remove: procedure
		.input(z.object({ profileId: z.string() }))
		.mutation(({ input, ctx }) => {
			ctx.pane.removeProfile(input.profileId);
		}),
});
