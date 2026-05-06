import { Menu } from "electron";
import { z } from "zod/v4";
import { procedure, router } from "../trpc";

function presentSurface(
	ctx: { surface: Electron.BrowserWindow },
	name: string,
	props?: Record<string, unknown>,
) {
	ctx.surface.show();

	ctx.surface.webContents.executeJavaScript(
		`window.postMessage(${JSON.stringify({ name, props })})`,
	);
}

export const uiRouter = router({
	present: procedure
		.input(
			z.object({
				name: z.string(),
				props: z.record(z.string(), z.unknown()).optional(),
			}),
		)
		.mutation(({ input, ctx }) => {
			presentSurface(ctx, input.name, input.props);
		}),

	dismiss: procedure.mutation(({ ctx }) => {
		ctx.surface.hide();
	}),

	profileContextMenu: procedure
		.input(z.object({ profileId: z.string() }))
		.mutation(({ input, ctx }) => {
			const menu = Menu.buildFromTemplate([
				{
					label: "Edit profile",
					click: () =>
						presentSurface(ctx, "ProfileSheet", {
							profileId: input.profileId,
						}),
				},
				{ type: "separator" },
				{
					label: "Delete profile",
					click: () => ctx.pane.removeProfile(input.profileId),
				},
			]);

			menu.popup();
		}),
});
