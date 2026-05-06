import { z } from "zod/v4";
import { procedure, router } from "../trpc";

export const uiRouter = router({
	present: procedure
		.input(
			z.object({
				name: z.string(),
				props: z.record(z.string(), z.unknown()).optional(),
			}),
		)
		.mutation(({ input, ctx }) => {
			ctx.surface.show();

			ctx.surface.webContents.executeJavaScript(
				`window.postMessage(${JSON.stringify({ name: input.name, props: input.props })})`,
			);
		}),

	dismiss: procedure.mutation(({ ctx }) => {
		ctx.surface.hide();
	}),
});
