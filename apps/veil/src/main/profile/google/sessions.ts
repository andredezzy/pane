import { type Profile, profileSession } from "../profile";
import { isGoogleCookieDomain } from "./domains";
import { clearGoogleSession } from "./sign-out";

// Cookies Google sets only for an authenticated account — anonymous visitors get
// NID/CONSENT but never these. Their presence on a Google domain means the profile
// is actually signed in, so "Sign out of Google" is worth offering.
const GOOGLE_AUTH_COOKIES = new Set([
	"SID",
	"HSID",
	"SSID",
	"APISID",
	"SAPISID",
	"__Secure-1PSID",
	"__Secure-3PSID",
	"__Secure-1PAPISID",
	"__Secure-3PAPISID",
]);

// A loaded profile also reloads its open Google tabs; an unloaded one is cleared
// straight off its persistent session so signing out never spins up the profile
// (and its proxy) as a side effect.
export function googleSignOut(
	findProfile: (id: string) => Profile | undefined,
	id: string,
): Promise<number> {
	const profile = findProfile(id);

	return profile
		? profile.signOutGoogle()
		: clearGoogleSession(profileSession(id));
}

// session.fromPartition returns the same Session for a profile whether it is loaded
// or not, so this reads the persistent cookies without loading the profile.
export async function googleIsSignedIn(id: string): Promise<boolean> {
	const cookies = await profileSession(id).cookies.get({});

	return cookies.some(
		(cookie) =>
			GOOGLE_AUTH_COOKIES.has(cookie.name) &&
			isGoogleCookieDomain(cookie.domain),
	);
}
