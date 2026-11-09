import { canDriveUI, controlEnabled, mainWindowOf } from "../lib/cua";
import { launchPane, tabOf } from "../lib/pane";
import { expect, type Scenario } from "../run";

// Back, forward, reload, and stop through the app, with the tab's URL, title,
// favicon, history state, and loading state following along. On an unlocked
// screen, the toolbar's Back and Forward are enabled only when the tab can go
// that way, read through accessibility.
const navigation: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Browse");
	const a = context.fixtures.url("nav-a.html");
	const b = context.fixtures.url("nav-b.html");

	const { tab, page: pageA } = await app.openPage(profile, a);

	const locked = !(await canDriveUI());
	const pid = app.browser.pid;
	const window = locked ? 0 : await mainWindowOf(pid);

	if (locked) {
		context.defer("the toolbar's arrows, read through accessibility");
	}

	const arrows: Record<string, unknown> = {};

	const expectArrows = async (at: string, back: boolean, forward: boolean) => {
		if (locked) {
			return;
		}

		await Bun.sleep(200);

		const shown = {
			back: await controlEnabled(pid, window, { title: "Back" }),
			forward: await controlEnabled(pid, window, { title: "Forward" }),
		};

		arrows[at] = shown;

		expect(
			shown.back === back && shown.forward === forward,
			`at ${at}, Back and Forward show ${JSON.stringify(shown)}`,
		);
	};

	const titled = await app.waitFor("title and favicon of A", (state) => {
		const current = tabOf(state, tab);

		return current?.title === "Navigation A" && current.hasFavicon;
	});

	expect(!tabOf(titled, tab)?.canGoBack, "a fresh tab cannot go back");
	await expectArrows("a fresh tab", false, false);

	await app.send("navigate", { input: b });

	await app.waitFor("B loaded with history", (state) => {
		const current = tabOf(state, tab);

		return (
			current?.url === b &&
			current.title === "Navigation B" &&
			current.canGoBack
		);
	});

	await expectArrows("B", true, false);

	await app.send("back");

	await app.waitFor("back at A", (state) => {
		const current = tabOf(state, tab);

		return current?.url === a && !current.loading && current.canGoForward;
	});

	await expectArrows("back at A", false, true);

	await app.send("forward");

	await app.waitFor("forward at B", (state) => {
		const current = tabOf(state, tab);

		return current?.url === b && !current.loading && !current.canGoForward;
	});

	await app.send("back");

	await app.waitFor(
		"back at A again",
		(state) => tabOf(state, tab)?.url === a && !tabOf(state, tab)?.loading,
	);

	const page = await app.page(tab, a);

	const loadsBefore = Number(
		await page.evaluate<string>("sessionStorage.loads"),
	);

	await app.send("reload");

	await app
		.waitFor(
			"the reload",
			(state) => tabOf(state, tab)?.loading === true,
			5_000,
		)
		.catch(() => {});

	await app.waitFor(
		"reload done",
		(state) => tabOf(state, tab)?.loading === false,
	);

	const reloaded = await app.page(tab, a);

	const loadsAfter = Number(
		await reloaded.evaluate<string>("sessionStorage.loads"),
	);

	expect(
		loadsAfter === loadsBefore + 1,
		`reload ran the page ${loadsAfter - loadsBefore} times`,
	);

	// A response held back 10 s: the tab reports loading, then stop ends it.
	const slow = context.fixtures.url("nav-b.html?delay=10000");
	await app.send("navigate", { input: slow });

	await app.waitFor(
		"the slow load",
		(state) => tabOf(state, tab)?.loading === true,
	);

	const stopStarted = Date.now();
	await app.send("stop");

	const stopped = await app.waitFor(
		"the stopped load",
		(state) => tabOf(state, tab)?.loading === false,
		5_000,
	);

	expect(Date.now() - stopStarted < 5_000, "stop did not end the load early");

	expect(
		tabOf(stopped, tab)?.url === a,
		`after stop the tab is at ${tabOf(stopped, tab)?.url}`,
	);

	// The address field follows the selected tab, across profiles too, even
	// while it has focus (⌘L) with nothing typed.
	await app.send("key", { key: "l", modifiers: ["command"] });

	await app.waitFor(
		"the address field focused",
		(state) => state.focus === "pane.toolbar.address",
	);

	const other = await app.createProfile("Other");
	const c = context.fixtures.url("nav-b.html?other");
	await app.openPage(other, c);

	await app.waitFor("the other tab's address", (state) => state.address === c);

	await app.send("select", { tab });

	const showsFirst = await app.waitFor(
		"the first tab's address again",
		(state) => state.address === tabOf(state, tab)?.url,
	);

	void pageA;

	// Under Settings and Extensions, which cover it, the selected tab's page
	// is hidden as a background tab is, so it stops drawing; it shows again
	// on the way back.
	const shownPage = await app.page(tab, tabOf(showsFirst, tab)?.url ?? "");

	const visibility = () =>
		shownPage.evaluate<string>("document.visibilityState");

	const pageVisibility: Record<string, string> = {
		browser: await visibility(),
	};

	await app.send("showPage", { page: "settings" });
	pageVisibility.settings = await visibility();
	await app.send("showPage", { page: "browser" });
	pageVisibility.back = await visibility();
	await app.send("extensionsMenu", { control: "manage" });
	pageVisibility.extensions = await visibility();
	await app.send("select", { tab });
	pageVisibility.selected = await visibility();

	expect(
		JSON.stringify(pageVisibility) ===
			JSON.stringify({
				browser: "visible",
				settings: "hidden",
				back: "visible",
				extensions: "hidden",
				selected: "visible",
			}),
		`the page's visibility: ${JSON.stringify(pageVisibility)}`,
	);
};

export default navigation;
