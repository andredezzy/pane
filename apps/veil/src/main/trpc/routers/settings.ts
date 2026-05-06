import type { SettingsState } from "../../../stores/settings-store";
import { detectBrowserPath } from "../../detect-browser";
import { procedure, router } from "../trpc";

export const settingsRouter = router({
	detectBrowser: procedure.mutation(({ ctx }) => {
		const detected = detectBrowserPath();

		if (detected) {
			const settings = ctx.stores["settings-store"].getState() as SettingsState;
			settings.save({ chromiumPath: detected });
		}

		return detected ?? null;
	}),
});
