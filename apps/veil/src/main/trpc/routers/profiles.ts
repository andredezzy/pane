import { net, session } from "electron";
import { z } from "zod/v4";

import { ProxyType } from "../../../stores/profile-store";
import { type TabState } from "../../../stores/tab-store";
import { procedure, router } from "../trpc";

const TEST_URL = "https://httpbin.org/ip";
const TEST_TIMEOUT_MS = 10_000;

function proxyFetch(
	testSession: Electron.Session,
	url: string,
	credentials?: { username: string; password: string },
): Promise<{ success: true; ip: string } | { success: false; error: string }> {
	return new Promise((resolve) => {
		const timeout = setTimeout(() => {
			request.abort();
			resolve({ success: false, error: "Connection timed out" });
		}, TEST_TIMEOUT_MS);

		const request = net.request({ url, session: testSession });

		if (credentials) {
			request.on("login", (_authInfo, callback) => {
				callback(credentials.username, credentials.password);
			});
		}

		request.on("response", (response) => {
			let body = "";

			response.on("data", (chunk) => {
				body += chunk.toString();
			});

			response.on("end", () => {
				clearTimeout(timeout);

				if (response.statusCode !== 200) {
					resolve({
						success: false,
						error: `HTTP ${response.statusCode}`,
					});
					return;
				}

				try {
					const data = JSON.parse(body) as { origin?: string };
					resolve({ success: true, ip: data.origin ?? "Unknown" });
				} catch {
					resolve({ success: false, error: "Invalid response" });
				}
			});
		});

		request.on("error", (error) => {
			clearTimeout(timeout);
			resolve({ success: false, error: error.message });
		});

		request.end();
	});
}

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

				return await proxyFetch(
					testSession,
					TEST_URL,
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
