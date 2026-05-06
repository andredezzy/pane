import { z } from "zod/v4";

import { procedure, router } from "../trpc";

export const cwsRouter = router({
	install: procedure
		.input(z.object({ extensionId: z.string() }))
		.mutation(({ input, ctx }) =>
			ctx.pane.extensions.install(input.extensionId),
		),

	uninstall: procedure
		.input(z.object({ extensionId: z.string() }))
		.mutation(({ input, ctx }) =>
			ctx.pane.extensions.uninstall(input.extensionId),
		),

	installed: procedure.query(({ ctx }) => ctx.pane.extensions.getInstalled()),
});
