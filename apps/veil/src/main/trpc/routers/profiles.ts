import { z } from "zod/v4";

import { procedure, router } from "../trpc";

export const profilesRouter = router({
	load: procedure
		.input(z.object({ profileId: z.string() }))
		.mutation(({ input, ctx }) => {
			ctx.pane.getOrCreateProfile(input.profileId).extensions.ensureLoaded();
		}),

	remove: procedure
		.input(z.object({ profileId: z.string() }))
		.mutation(({ input, ctx }) => {
			ctx.pane.removeProfile(input.profileId);
		}),
});
