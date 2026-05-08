import { session } from "electron";
import { z } from "zod/v4";

import { ProxyType } from "../../../stores/profile-store";
import { type TabState } from "../../../stores/tab-store";
import { testProxyConnection } from "../../profile/proxy-test";
import { procedure, router } from "../trpc";

export const profilesRouter = router({
	testProxy: procedure
		.input(
			z.object({
				proxyType: z.nativeEnum(ProxyType),
				host: z.string().min(1),
				port: z.number().int().min(1).max(65535),
				username: z.string().optional(),
				password: z.string().optional(),
			}),
		)
		.mutation(async ({ input }) => {
			const partition = `temp:proxy-test-${Date.now()}`;
			const testSession = session.fromPartition(partition, { cache: false });

			const proxyUrl = `${input.proxyType.toLowerCase()}://${input.host}:${input.port}`;

			try {
				await testSession.setProxy({ proxyRules: proxyUrl });

				return await testProxyConnection(
					testSession,
					input.username
						? {
								username: input.username,
								password: input.password ?? "",
							}
						: undefined,
				);
			} catch (error) {
				const message =
					error instanceof Error ? error.message : "Connection failed";

				return { success: false as const, error: message };
			}
		}),

	load: procedure
		.input(z.object({ profileId: z.string() }))
		.mutation(({ input, ctx }) => {
			ctx.pane.getOrCreateProfile(input.profileId).extensions.ensureLoaded();
		}),

	unload: procedure
		.input(z.object({ profileId: z.string() }))
		.mutation(({ input, ctx }) => {
			const profile = ctx.pane.getProfile(input.profileId);

			if (!profile) {
				return;
			}

			const tabState = ctx.stores["tab-store"].getState() as TabState;

			if (tabState.activeProfileId === input.profileId) {
				tabState.setActiveTab(null, null);
			}

			profile.tabs.unloadAll();
		}),

	remove: procedure
		.input(z.object({ profileId: z.string() }))
		.mutation(({ input, ctx }) => {
			ctx.pane.removeProfile(input.profileId);
		}),
});
