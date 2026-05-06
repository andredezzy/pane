import { cwsRouter } from "./routers/cws";
import { extensionsRouter } from "./routers/extensions";
import { profilesRouter } from "./routers/profiles";
import { securityRouter } from "./routers/security";
import { settingsRouter } from "./routers/settings";
import { storesRouter } from "./routers/stores";
import { tabsRouter } from "./routers/tabs";
import { uiRouter } from "./routers/ui";
import { router } from "./trpc";

export const appRouter = router({
	tabs: tabsRouter,
	profiles: profilesRouter,
	settings: settingsRouter,
	security: securityRouter,
	extensions: extensionsRouter,
	cws: cwsRouter,
	stores: storesRouter,
	ui: uiRouter,
});

export type AppRouter = typeof appRouter;
