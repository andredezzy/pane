import { type AppState, launchPane, tabOf } from "../lib/pane";
import { expect, type Scenario } from "../run";

// A tab is loading only while a new document loads in its main frame, as
// Chrome's own loading UI shows it, Aside's and Dia's too. An iframe that
// loads under a page already shown (an embed as the tab comes forward, an ad
// as it refreshes) is not the page loading: the address bar showed its
// progress for as long, on tabs long loaded (the user's report).
const pageLoading: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Loading");
	const url = context.fixtures.url("widget.html");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);

	await app.waitFor(
		"the page loaded",
		(state) => tabOf(state, tab)?.loading === false,
	);

	// What the tab reported every 50 ms while `during` ran.
	const watch = async (during: Promise<unknown>) => {
		const seen: boolean[] = [];
		let done = false;

		during.finally(() => {
			done = true;
		});

		while (!done) {
			seen.push(tabOf(await app.state(), tab)?.loading ?? false);
			await Bun.sleep(50);
		}

		await during;

		return seen;
	};

	// An iframe that takes 1.5 s to load, on the loaded page.
	const started = Date.now();
	const widget = await watch(page.evaluate("addWidget(1500)"));
	const widgetMs = Date.now() - started;

	expect(
		widgetMs >= 1_400,
		`the widget loaded in ${widgetMs} ms, too fast to tell`,
	);

	expect(
		widget.length > 10 && widget.every((loading) => !loading),
		`the tab showed as loading while an iframe loaded: ${JSON.stringify(widget)}`,
	);

	// Another tab forward, then this one again: it shows as loaded at once.
	const other = await app.openTab(profile, context.fixtures.url("nav-a.html"));
	await app.waitFor("the other tab", (state) => state.activeTabID === other);
	await app.send("select", { tab });

	const back = await app.waitFor(
		"the tab forward again",
		(state) => state.activeTabID === tab,
	);

	expect(
		tabOf(back, tab)?.loading === false,
		"the loaded tab showed as loading when it came forward",
	);

	// A new document in the main frame still shows, from its start to its
	// end.
	const slow = context.fixtures.url("nav-b.html?delay=1500");
	await app.send("navigate", { input: slow });

	const loading = await app.waitFor(
		"the navigation loading",
		(state: AppState) => tabOf(state, tab)?.loading === true,
		1_000,
	);

	const loaded = await app.waitFor(
		"the navigation loaded",
		(state: AppState) =>
			tabOf(state, tab)?.url === slow && tabOf(state, tab)?.loading === false,
	);

	return {
		widget: {
			ms: widgetMs,
			samples: widget.length,
			loading: widget.some(Boolean),
		},
		forwardAgain: tabOf(back, tab)?.loading,
		navigation: {
			loading: tabOf(loading, tab)?.loading,
			loaded: tabOf(loaded, tab)?.loading,
		},
	};
};

export default pageLoading;
