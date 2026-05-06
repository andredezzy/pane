import { Menu, nativeImage } from "electron";
import { z } from "zod/v4";
import { procedure, router } from "../trpc";

const menuItemSchema = z.union([
	z.object({
		id: z.string(),
		label: z.string(),
		icon: z.string().optional(),
		enabled: z.boolean().optional(),
	}),
	z.object({ type: z.literal("separator") }),
]);

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

	menu: procedure
		.input(z.object({ items: z.array(menuItemSchema) }))
		.mutation(({ input }) => {
			return new Promise<string | null>((resolve) => {
				let selected: string | null = null;

				const template = input.items.map((item) => {
					if ("type" in item) {
						return { type: "separator" as const };
					}

					let icon: Electron.NativeImage | undefined;

					if (item.icon) {
						icon = nativeImage
							.createFromDataURL(item.icon)
							.resize({ width: 16, height: 16 });

						if (process.platform === "darwin") {
							icon.setTemplateImage(true);
						}
					}

					return {
						label: item.label,
						icon,
						enabled: item.enabled ?? true,
						click: () => {
							selected = item.id;
						},
					};
				});

				const menu = Menu.buildFromTemplate(template);

				menu.popup({ callback: () => resolve(selected) });
			});
		}),
});
