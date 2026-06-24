import { isGoogleCookieDomain } from "./domains";

// Origin-keyed storage (localStorage / IndexedDB / service workers) that Google's
// account and sign-in widgets read. Cookies carry the actual auth; clearing this
// resets the stale GAIA state that otherwise leaves accounts.google.com looping on
// error #2014 after a sign-out.
const GOOGLE_STORAGE_ORIGINS = [
	"https://accounts.google.com",
	"https://myaccount.google.com",
	"https://www.google.com",
	"https://google.com",
	"https://mail.google.com",
	"https://drive.google.com",
	"https://docs.google.com",
	"https://calendar.google.com",
	"https://photos.google.com",
	"https://play.google.com",
	"https://ogs.google.com",
	"https://apis.google.com",
	"https://www.youtube.com",
	"https://youtube.com",
	"https://m.youtube.com",
	"https://studio.youtube.com",
	"https://music.youtube.com",
];

export async function clearGoogleSession(
	targetSession: Electron.Session,
): Promise<number> {
	const cookies = await targetSession.cookies.get({});

	const googleCookies = cookies.filter(
		(cookie): cookie is Electron.Cookie & { domain: string } =>
			isGoogleCookieDomain(cookie.domain),
	);

	let count = 0;

	for (const cookie of googleCookies) {
		const url = `http${cookie.secure ? "s" : ""}://${cookie.domain.replace(/^\./, "")}${cookie.path ?? "/"}`;

		try {
			await targetSession.cookies.remove(url, cookie.name);
			count++;
		} catch (error) {
			console.warn(`[SignOut] Failed to remove cookie ${cookie.name}:`, error);
		}
	}

	try {
		await targetSession.clearData({
			origins: GOOGLE_STORAGE_ORIGINS,
			dataTypes: [
				"cache",
				"localStorage",
				"indexedDB",
				"serviceWorkers",
				"backgroundFetch",
				"fileSystems",
			],
		});
	} catch (error) {
		console.warn("[SignOut] Failed to clear Google site storage:", error);
	}

	return count;
}
