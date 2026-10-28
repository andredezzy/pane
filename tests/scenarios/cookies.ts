import { launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

// A profile's cookies as the engine reports them (PaneCookie), with the
// attributes the site set: Secure, HttpOnly, SameSite, and expiry (none for
// a session cookie), and their domain and path. Values are never read.
const cookies: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Cookies");
	const url = context.fixtures.url("cookie-attributes");
	await app.page(await app.openTab(profile, url), url);

	const setAt = Date.now() / 1000;
	await app.send("readCookies", { profile });

	const read = await app.waitFor(
		"the profile's cookies",
		(state) =>
			(state.profiles.find((each) => each.id === profile)?.cookies?.length ??
				0) >= 4,
	);

	const all = read.profiles.find((each) => each.id === profile)?.cookies ?? [];
	const named = (name: string) => all.find((cookie) => cookie.name === name);
	const lax = named("pane_lax");

	const checks = {
		session:
			named("pane_session")?.expires === null &&
			named("pane_session")?.sameSite === "unspecified" &&
			named("pane_session")?.secure === false &&
			named("pane_session")?.httpOnly === false,
		lax:
			lax?.sameSite === "lax" &&
			typeof lax.expires === "number" &&
			Math.abs(lax.expires - (setAt + 3600)) < 120,
		strict:
			named("pane_strict")?.sameSite === "strict" &&
			named("pane_strict")?.httpOnly === true &&
			named("pane_strict")?.path === "/cookie-attributes",
		none:
			named("pane_none")?.sameSite === "none" &&
			named("pane_none")?.secure === true,
		hostOnly: all.every((cookie) => cookie.domain === "127.0.0.1"),
	};

	expect(
		Object.values(checks).every(Boolean),
		`cookies as read: ${JSON.stringify({ checks, all })}`,
	);

	return { checks, cookies: all };
};

export default cookies;
