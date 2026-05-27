import { session } from "electron";
import { z } from "zod/v4";

import { ProxyType } from "../../../stores/profile-store";
import { type TabState } from "../../../stores/tab-store";
import { ProxyRelay } from "../../profile/proxy-relay";
import { testProxyConnection } from "../../profile/proxy-test";
import { procedure, router } from "../trpc";

const PROXY_TEST_POOL_SIZE = 4;
let proxyTestIndex = 0;

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
			const testSession = session.fromPartition(
				`temp:proxy-test-${proxyTestIndex++ % PROXY_TEST_POOL_SIZE}`,
				{ cache: false },
			);

			const relay = new ProxyRelay({
				proxyType: input.proxyType,
				host: input.host,
				port: input.port,
				username: input.username ?? null,
				password: input.password ?? null,
			});

			try {
				await relay.start();
				await testSession.setProxy({ proxyRules: relay.proxyUrl });

				const result = await testProxyConnection(
					testSession,
					input.username && !relay.needsRelay
						? {
								username: input.username,
								password: input.password ?? "",
							}
						: undefined,
				);

				relay.stop();

				return result;
			} catch (error) {
				relay.stop();

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
