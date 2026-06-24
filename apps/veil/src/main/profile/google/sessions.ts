import { type Profile, profileSession } from "../profile";
import { clearGoogleSession } from "./sign-out";

export class GoogleSessions {
	constructor(private readonly resolve: (id: string) => Profile | undefined) {}

	// A loaded profile also reloads its open Google tabs; an unloaded one is
	// cleared straight off its persistent session so signing out never spins up
	// the profile (and its proxy) as a side effect.
	signOut(id: string): Promise<number> {
		const profile = this.resolve(id);

		return profile
			? profile.signOutGoogle()
			: clearGoogleSession(profileSession(id));
	}
}
