import { launchPane } from "../lib/pane";
import { poll } from "../lib/poll";
import { expect, type Scenario } from "../run";

// G6 and H7 on the Views build (docs/plans/views-migration.md, Phases 1 and
// 3): keys with no router, through the menu bar's key equivalents, the
// window's FocusManager, and the page's PreHandleKeyboardEvent. ⌘T goes
// ahead of the page and ⌘L to the page first, as the Swift ordered them (ADR
// 0019): a page that takes ⌘L keeps it, and one that would take ⌘T never
// sees it. A key typed right behind ⌘T lands in the new tab's address; ⌘1 to
// ⌘8 choose a profile's tab by its place, none past its last (the Swift's
// selectTab(atIndex:)), and ⌘9 its last (Chrome's and Safari's, the user's
// call); Esc and Return close the sheet; Quit Pane? takes
// every key, the menu bar's included, but ⌘Q; the switcher opens on Ctrl-Tab
// and chooses on Ctrl's release. Real keys, posted through the window's event
// path, so the window must be in front (run.ts --foreground).
const viewsKeys: Scenario = async (context) => {
	if (!context.foreground) {
		context.defer("every key: it needs Pane in front (run.ts --foreground)");

		return;
	}

	const app = await launchPane(context);

	const profile = await app.createProfile("Keys");
	const pages: Awaited<ReturnType<typeof app.openPage>>[] = [];

	for (const name of ["one", "two", "three"]) {
		pages.push(
			await app.openPage(profile, context.fixtures.url(`nav-a.html?${name}`)),
		);
	}

	const tabs = async () =>
		(await app.state()).profiles.find((entry) => entry.id === profile)?.tabs ??
		[];

	// The page has the keyboard; ⌘T still opens a tab, before it sees the key.
	expect((await app.state()).focus === "pane.content", "the page has no focus");
	const before = (await tabs()).length;
	await app.send("key", { key: "t", modifiers: ["command"] });
	// A key queued right behind ⌘T lands in the new tab's address.
	await app.send("key", { key: "a" });

	await poll(
		"the new tab, its address typed in",
		async () => {
			const state = await app.state();

			return (
				(await tabs()).length === before + 1 &&
				state.focus === "pane.toolbar.address" &&
				state.address === "a"
			);
		},
		10_000,
	);

	// ⌘1 and ⌘3 choose by place; ⌘9, with four tabs, the last.
	for (const [key, index] of [
		["1", 0],
		["3", 2],
		["9", 3],
	] as const) {
		await app.send("key", { key, modifiers: ["command"] });
		const want = (await tabs())[index];

		await poll(
			`⌘${key}`,
			async () => (await app.state()).activeTabID === want?.id,
			5_000,
		);
	}

	// ⌘L from the page.
	await app.send("select", { tab: pages[0]?.tab });
	await app.send("key", { key: "l", modifiers: ["command"] });

	await poll(
		"⌘L",
		async () => (await app.state()).focus === "pane.toolbar.address",
		5_000,
	);

	// A page that takes ⌘L and ⌘T: ⌘L stays with it, ⌘T opens a tab and the
	// page never hears of it.
	await app.send("select", { tab: pages[0]?.tab });

	await poll(
		"the page",
		async () => (await app.state()).focus === "pane.content",
		5_000,
	);

	await pages[0]?.page.evaluate(`(() => {
		window.taken = [];
		addEventListener("keydown", (event) => {
			if (event.metaKey && (event.key === "l" || event.key === "t")) {
				window.taken.push(event.key);
				event.preventDefault();
			}
		});
	})()`);

	await app.send("key", { key: "l", modifiers: ["command"] });

	await poll(
		"⌘L taken by the page",
		async () =>
			JSON.stringify(await pages[0]?.page.evaluate("window.taken")) === '["l"]',
		5_000,
	);

	await Bun.sleep(300);
	expect((await app.state()).focus === "pane.content", "⌘L left the page");
	const count = (await tabs()).length;
	await app.send("key", { key: "t", modifiers: ["command"] });

	await poll(
		"⌘T ahead of the page",
		async () => (await tabs()).length === count + 1,
		5_000,
	);

	expect(
		JSON.stringify(await pages[0]?.page.evaluate("window.taken")) === '["l"]',
		"the page heard ⌘T",
	);

	// The sheet over the page takes the keys: Esc closes it, and Return
	// creates, which a name missing stops, its field ringed, then Esc closes
	// it.
	for (const key of ["\u001b", "\r"]) {
		await app.send("openProfileSheet");

		await poll(
			"the sheet's name taking the keys",
			async () => (await app.state()).focus === "pane.profile.name",
			5_000,
		);

		await app.send("key", { key });

		if (key === "\r") {
			await poll(
				"Return stopped by the missing name",
				async () =>
					(
						await app.send<Record<string, { description: string }>>(
							"accessibility",
							{ prefix: "pane.profile.name" },
						)
					).result["pane.profile.name"]?.description ===
					"Profile name is required",
				5_000,
			);

			await app.send("key", { key: "\u001b" });
		}

		await poll(
			`the sheet closing on ${key === "\r" ? "Return, then Esc" : "Esc"}`,
			async () => (await app.state()).profileSheet === null,
			5_000,
		);
	}

	// Quit Pane? takes every key: ⌘T, a menu bar's key, opens no tab; Esc
	// goes back.
	await app.send("select", { tab: pages[1]?.tab });
	await app.send("key", { key: "q", modifiers: ["command"] });

	await poll(
		"Quit Pane?",
		async () => (await app.state()).quitConfirmation,
		5_000,
	);

	const asking = (await tabs()).length;
	await app.send("key", { key: "t", modifiers: ["command"] });
	await app.send("key", { key: "a" });
	await Bun.sleep(500);

	expect(
		(await tabs()).length === asking &&
			(await app.state()).focus === "pane.quit",
		"a key reached past Quit Pane?",
	);

	await app.send("key", { key: "\u001b" });

	await poll(
		"Quit Pane? gone, the page focused",
		async () => {
			const state = await app.state();

			return !state.quitConfirmation && state.focus === "pane.content";
		},
		5_000,
	);

	// Ctrl-Tab opens the switcher, Ctrl's release chooses.
	const shown = (await app.state()).activeTabID;
	await app.send("key", { key: "\t", modifiers: ["control"] });

	await poll(
		"the switcher",
		async () => (await app.state()).switcher.visible,
		5_000,
	);

	await app.send("releaseModifiers");

	await poll(
		"the switcher's choice",
		async () => {
			const state = await app.state();

			return !state.switcher.visible && state.activeTabID !== shown;
		},
		5_000,
	);

	await app.quit();
};

export default viewsKeys;
