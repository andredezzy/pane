import { launchPane } from "../lib/pane";
import { settledWindowsOf } from "../lib/windows";
import { expect, type Scenario } from "../run";

// Two profiles' tabs share the one window, and storage set by a page in
// profile A is invisible to the same site in profile B.
const profilesIsolation: Scenario = async (context) => {
	const app = await launchPane(context);
	const work = await app.createProfile("Work", "BLUE");
	const personal = await app.createProfile("Personal", "ROSE");

	const workURL = context.fixtures.url("isolation.html?profile=work");
	const workTab = await app.openTab(work, workURL);
	const workPage = await app.page(workTab, workURL);

	await workPage.evaluate(
		"document.cookie = 'pane=work; max-age=3600'; localStorage.pane = 'work'",
	);

	const personalURL = context.fixtures.url("isolation.html?profile=personal");
	const personalTab = await app.openTab(personal, personalURL);
	const personalPage = await app.page(personalTab, personalURL);

	const read =
		"({ cookie: document.cookie, storage: localStorage.pane ?? null })";

	const inPersonal = await personalPage.evaluate<{
		cookie: string;
		storage: string | null;
	}>(read);

	const inWork = await workPage.evaluate<{
		cookie: string;
		storage: string | null;
	}>(read);

	expect(
		inWork.cookie === "pane=work",
		`work cookie was ${JSON.stringify(inWork.cookie)}`,
	);

	expect(inWork.storage === "work", "work localStorage lost its value");

	expect(
		inPersonal.cookie === "",
		`personal saw cookie ${JSON.stringify(inPersonal.cookie)}`,
	);

	expect(inPersonal.storage === null, "personal saw work's localStorage");

	const { targetInfos } = await app.browser.cdp.send<{
		targetInfos: { url: string; type: string; browserContextId?: string }[];
	}>("Target.getTargets");

	const contextOf = (url: string) =>
		targetInfos.find((target) => target.url === url)?.browserContextId;

	expect(
		contextOf(workURL) !== contextOf(personalURL),
		"both tabs report the same browser context",
	);

	const windows = await settledWindowsOf(app.browser.pid, 1);

	expect(
		windows.count === 1,
		`expected 1 window, got ${JSON.stringify(windows.windows)}`,
	);

	const state = await app.state();

	expect(
		state.profiles.map((profile) => profile.tabs.length).join(",") === "1,1",
		"each profile should hold its one tab",
	);

	return {
		windows,
		work: { tab: workTab, ...inWork, browserContextId: contextOf(workURL) },
		personal: {
			tab: personalTab,
			...inPersonal,
			browserContextId: contextOf(personalURL),
		},
		profiles: state.profiles,
	};
};

export default profilesIsolation;
