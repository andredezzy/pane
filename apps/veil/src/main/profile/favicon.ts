import { type BrowserProfile, profileStore } from "../../stores/profile-store";

// Favicons are inlined into the persisted profile as data URLs; anything larger
// than this stays a remote reference instead of bloating profiles.json.
const MAX_FAVICON_BYTES = 64 * 1024;

// Exactly what the inliner needs from a profile — not the whole TabHost.
export interface FaviconHost {
	readonly id: string;
	readonly session: Electron.Session;
	readonly proxyReady: Promise<boolean>;
	readonly data: Pick<BrowserProfile, "tabs">;
}

export async function fetchFaviconAsDataUrl(
	session: Electron.Session,
	faviconUrl: string,
): Promise<string | null> {
	const response = await session.fetch(faviconUrl);

	if (!response.ok) {
		return null;
	}

	const bytes = Buffer.from(await response.arrayBuffer());

	if (bytes.length === 0 || bytes.length > MAX_FAVICON_BYTES) {
		return null;
	}

	const contentType = response.headers.get("content-type") ?? "image/png";

	return `data:${contentType};base64,${bytes.toString("base64")}`;
}

/**
 * Inline every tab favicon that is still a remote reference — guessing
 * origin/favicon.ico when none was ever captured — through the profile's own
 * session, so the sidebar renders icons instantly instead of fetching them on
 * every expand. Waits for proxyReady: favicon requests must ride the profile's
 * proxy, never the direct connection.
 */
export function inlineProfileFavicons(profile: FaviconHost): void {
	void profile.proxyReady.then(() => {
		for (const tab of profile.data.tabs) {
			if (tab.favicon.startsWith("data:")) {
				continue;
			}

			let target = tab.favicon;

			if (!target) {
				try {
					target = `${new URL(tab.url).origin}/favicon.ico`;
				} catch {
					continue;
				}
			}

			fetchFaviconAsDataUrl(profile.session, target)
				.then((dataUrl) => {
					if (dataUrl) {
						profileStore.getState().updateTab(profile.id, tab.id, {
							favicon: dataUrl,
						});
					}
				})
				.catch(() => {
					// Keep whatever reference the store already has.
				});
		}
	});
}
