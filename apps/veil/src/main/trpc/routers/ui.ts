import { z } from "zod/v4";
import { procedure, router } from "../trpc";

export const uiRouter = router({
	present: procedure
		.input(z.object({ name: z.string() }))
		.mutation(({ input, ctx }) => {
			ctx.surface.show();

			ctx.surface.webContents.executeJavaScript(
				`window.postMessage(${JSON.stringify({ name: input.name })})`,
			);
		}),
	dismiss: procedure.mutation(({ ctx }) => {
		ctx.surface.hide();
	}),

	exitSurfaceMode: procedure.mutation(({ ctx }) => {
		ctx.pane.exitSurfaceMode();
	}),
});
