import { bringToFront, launchPane, tabOf } from "../lib/pane";
import { bySystem } from "../lib/platform";
import { expect, type Scenario } from "../run";

// The keys between the window's groups on Windows, as Chrome's between its
// panes (ADR 0019, focus_groups.h): F6 moves them from the page to the
// address, the toolbar's first button, the sidebar's first, and the page
// again; Shift+F6 goes back; in a group the arrows move between its
// buttons, and Esc gives the keys back to the page; Alt+Shift+T takes them
// to the toolbar's first button. The toolbar's and the sidebar's buttons
// take no Tab: from the address, Shift+Tab lands on none of them. On the
// Mac none of it: F6 and Alt+Shift+T leave the keys where they are, and no
// button takes Tab there either.
const WINDOWS = bySystem({ mac: false, windows: true });

const focusGroups: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Groups");
	const a = context.fixtures.url("nav-a.html");
	const b = context.fixtures.url("nav-b.html");
	const { tab } = await app.openPage(profile, a);

	await app.waitFor(
		"A loaded",
		(state) => tabOf(state, tab)?.loading === false,
	);

	// A page back, so the toolbar's first button, Back, takes the keys.
	await app.send("navigate", { input: b });

	await app.waitFor(
		"B loaded with history",
		(state) => tabOf(state, tab)?.url === b && !!tabOf(state, tab)?.canGoBack,
	);

	// The window's keys run while it is the active one, on Windows.
	if (!(await bringToFront(context, app, "the keys between groups"))) {
		return;
	}

	await app.send("clickPage", { x: 20, y: 20 });

	await app.waitFor(
		"the page focused",
		(state) => state.focus === "pane.content",
	);

	const press = async (key: string, modifiers: string[] = []) => {
		await app.send("key", { key, modifiers });
		// Focus moves on the turn after the key.
		await Bun.sleep(300);

		return (await app.state()).focus;
	};

	if (!WINDOWS) {
		const afterF6 = await press("f6");
		const afterT = await press("t", ["option", "shift"]);

		expect(
			afterF6 === "pane.content" && afterT === "pane.content",
			`the Mac moved the keys: F6 ${afterF6}, Alt+Shift+T ${afterT}`,
		);

		return;
	}

	const steps = {
		address: await press("f6"),
		toolbar: await press("f6"),
		toolbarRight: await press("right"),
		sidebar: await press("f6"),
		sidebarDown: await press("down"),
		page: await press("f6"),
		backToSidebar: await press("f6", ["shift"]),
		escaped: await press("\u001b"),
		altShiftT: await press("t", ["option", "shift"]),
		escapedAgain: await press("\u001b"),
	};

	expect(
		steps.address === "pane.toolbar.address" &&
			steps.toolbar === "pane.toolbar.back" &&
			// Forward is disabled, so the arrow passes it by.
			steps.toolbarRight === "pane.toolbar.reload" &&
			// "Pane ⌄", the sidebar's first button, under its header.
			steps.sidebar === "pane.sidebar.header" &&
			steps.sidebarDown === "pane.sidebar.collapseAll" &&
			steps.page === "pane.content" &&
			steps.backToSidebar === "pane.sidebar.header" &&
			steps.escaped === "pane.content" &&
			steps.altShiftT === "pane.toolbar.back" &&
			steps.escapedAgain === "pane.content",
		`the keys between groups: ${JSON.stringify(steps)}`,
	);

	// From the address, Shift+Tab: a field or the page takes the keys, never
	// a button of the toolbar's or the sidebar's.
	await press("f6");
	const shiftTab = await press("\t", ["shift"]);
	const buttons = /^pane\.(toolbar\.(back|forward|reload|profile)|sidebar\.)/;

	expect(
		!buttons.test(shiftTab),
		`Shift+Tab from the address went to a button: ${shiftTab}`,
	);
};

export default focusGroups;
