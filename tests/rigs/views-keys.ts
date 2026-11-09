import { launchPane } from "../lib/pane";
import { poll } from "../lib/poll";
import { expect, type Scenario } from "../run";

// G6 on the Views prototype (docs/plans/views-migration.md, Phase 1): keys
// with no router, through the window's FocusManager and the page's
// PreHandleKeyboardEvent. ⌘T and ⌘L go ahead of the page; a key typed right
// behind ⌘T lands in the new tab's address; ⌘1 to ⌘9 choose a profile's tab
// by its place; Esc and Return close the sheet. Real keys, posted through the
// window's event path, so the window must be in front (run.ts --foreground).
const viewsKeys: Scenario = async (context) => {
	if (!context.foreground) {
		context.defer("every key: it needs Pane in front (run.ts --foreground)");

		return;
	}

	const app = await launchPane(context);
	const profile = await app.createProfile("Keys");
	const pages = [];

	for (const name of ["one", "two", "three"]) {
		pages.push(
			await app.openPage(profile, context.fixtures.url(`nav-a.html?${name}`)),
		);
	}

	const tabs = async () =>
		(await app.state()).profiles.find((entry) => entry.id === profile)?.tabs ??
		[];

	// The page has the keyboard; ⌘T still opens a tab, before it sees the key.
	expect((await app.state()).focus === "page", "the page has no focus");
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
				state.focus === "address" &&
				state.address === "a"
			);
		},
		10_000,
	);

	// ⌘1 and ⌘3 choose by place, ⌘9 the last.
	for (const [key, index] of [
		["1", 0],
		["3", 2],
		["9", -1],
	] as const) {
		await app.send("key", { key, modifiers: ["command"] });
		const list = await tabs();
		const want = index < 0 ? list[list.length - 1] : list[index];

		await poll(
			`⌘${key}`,
			async () => (await app.state()).activeTabID === want?.id,
			5_000,
		);
	}

	// ⌘L from the page.
	await app.send("select", { tab: pages[0]?.tab });
	await app.send("key", { key: "l", modifiers: ["command"] });

	await poll("⌘L", async () => (await app.state()).focus === "address", 5_000);

	// Esc, then Return, close the sheet over the page.
	for (const key of ["\u001b", "\r"]) {
		await app.send("openProfileSheet");
		expect((await app.state()).focus === "sheet", "the sheet has no focus");
		await app.send("key", { key });

		await poll(
			`the sheet closing on ${key === "\r" ? "Return" : "Esc"}`,
			async () => (await app.state()).focus !== "sheet",
			5_000,
		);
	}

	await app.quit();
};

export default viewsKeys;
