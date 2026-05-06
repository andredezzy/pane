import { z } from "zod/v4";

import { procedure, router } from "../trpc";

export const extensionsRouter = router({
	list: procedure
		.input(z.object({ profileId: z.string() }))
		.query(({ input, ctx }) => {
			const loaded =
				ctx.pane.getProfile(input.profileId)?.extensions.getLoaded() ?? [];

			return loaded.map((ext) => ({
				id: ext.id,
				name: ext.name,
				version: ext.manifest.version,
			}));
		}),
});
