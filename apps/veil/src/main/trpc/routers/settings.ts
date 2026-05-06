import type { SettingsState } from "../../../stores/settings-store";
import { autoDetectBrowser } from "../../detect-browser";
import { procedure, router } from "../trpc";

export const settingsRouter = router({
	detectBrowser: procedure.mutation(({ ctx }) => {
		const settings = ctx.stores["settings-store"].getState() as SettingsState;

		return autoDetectBrowser(settings.update) ?? null;
	}),
});
