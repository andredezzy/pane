import { type Profile, profileSession } from "../profile";
import { clearGoogleSession } from "./sign-out";

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
