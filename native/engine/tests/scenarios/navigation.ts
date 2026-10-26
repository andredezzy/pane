import { launchPane, tabOf } from "../lib/pane";
import { expect, type Scenario } from "../run";

// Back, forward, reload, and stop through the app, with the tab's URL, title,
// favicon, history state, and loading state following along.
const navigation: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Browse");
	const a = context.fixtures.url("nav-a.html");
	const b = context.fixtures.url("nav-b.html");

	const tab = await app.openTab(profile, a);
	const pageA = await app.page(tab, a);

	const titled = await app.waitFor("title and favicon of A", (state) => {
		const current = tabOf(state, tab);

		return current?.title === "Navigation A" && current.hasFavicon;
	});

	expect(!tabOf(titled, tab)?.canGoBack, "a fresh tab cannot go back");

	await app.send("navigate", { input: b });

	await app.waitFor("B loaded with history", (state) => {
		const current = tabOf(state, tab);

		return (
			current?.url === b &&
			current.title === "Navigation B" &&
			current.canGoBack
		);
	});

	await app.send("back");

	const back = await app.waitFor("back at A", (state) => {
		const current = tabOf(state, tab);

		return current?.url === a && !current.loading && current.canGoForward;
	});

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

	const loading = await app.waitFor(
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
		(state) => state.focus === "field: Search or enter URL",
	);

	const other = await app.createProfile("Other");
	const c = context.fixtures.url("nav-b.html?other");
	const otherTab = await app.openTab(other, c);
	await app.page(otherTab, c);

	const showsOther = await app.waitFor(
		"the other tab's address",
		(state) => state.address === c,
	);

	await app.send("select", { tab });

	const showsFirst = await app.waitFor(
		"the first tab's address again",
		(state) => state.address === tabOf(state, tab)?.url,
	);

	void pageA;

	return {
		titled: tabOf(titled, tab),
		back: tabOf(back, tab),
		reloads: { before: loadsBefore, after: loadsAfter },
		slowLoad: { loading: tabOf(loading, tab), stopped: tabOf(stopped, tab) },
		address: { other: showsOther.address, first: showsFirst.address },
	};
};

export default navigation;
