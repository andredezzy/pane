import { join } from "node:path";
import { canReadUI } from "../lib/computer-use";
import { extensionId } from "../lib/extensions";
import {
	type AppState,
	launchPane,
	pressWindowKey,
	QUIT_WORD,
	RETURN_PRESSES_THE_RING,
} from "../lib/pane";
import { bySystem } from "../lib/platform";
import { pollValue } from "../lib/poll";
import { waitForSheet } from "../lib/surfaces";
import { type AccessibleElement, accessibilityTree } from "../lib/windows";
import { expect, type Scenario } from "../run";

const PROBE = join(
	import.meta.dir,
	"..",
	"pages",
	"extensions",
	"action-probe",
);

const NAME = "Pane action probe";

// An element's name as a screen reader reads it: its title, else its
// description, where the Mac's groups and dialogs carry it.
const nameOf = (element: AccessibleElement) =>
	element.title || element.description;

type Read = (element: AccessibleElement) => boolean;

// A profile's row: its name, its place among the profiles, its tabs, and
// whether it shows them.
type Row = { name: string; place: string; tabs: number; expanded: boolean };

const tabsOf = (count: number) => `${count} ${count === 1 ? "tab" : "tabs"}`;

// What each system's screen reader is given of Pane's views (ADR 0071):
// VoiceOver's accessibility on the Mac, the AX roles it has read since ADR
// 0071; UI Automation on Windows, Narrator's, the roles and states it reads
// right (pane/ui/design/screen_reader.h): the sidebar a tree, a profile's
// row named with its tabs, a picker a combo box valued at its option, a
// choice a radio button at its place, the Settings button a toggle, a
// heading at its level, the PIN's count the PIN field's description, and the sidebar's edge a
// slider. Neither reads a state the other's tree lacks:
// the Mac's tool reads no selection, which VoiceOver takes from AXSelected.
const READS = bySystem({
	mac: {
		row:
			({ name, tabs, expanded }: Row): Read =>
			(element) =>
				element.role === "AXButton" &&
				nameOf(element) === name &&
				element.value ===
					`${tabsOf(tabs)}, ${expanded ? "expanded" : "collapsed"}`,
		tab:
			(title: string, _place: string, _shown: boolean): Read =>
			(element) =>
				element.role === "AXButton" && nameOf(element) === title,
		splitter:
			(width: number): Read =>
			(element) =>
				element.role === "AXSplitter" &&
				nameOf(element) === "Sidebar width" &&
				element.value === String(width),
		settings:
			(_on: boolean): Read =>
			(element) =>
				element.role === "AXButton" && nameOf(element) === "Settings",
		heading:
			(name: string, _level: number): Read =>
			(element) =>
				element.role === "AXHeading" && nameOf(element) === name,
		picker:
			(name: string, value: string): Read =>
			(element) =>
				element.role === "AXPopUpButton" &&
				nameOf(element) === name &&
				element.value === value,
		dialog:
			(name: string): Read =>
			(element) =>
				element.subrole === "AXApplicationDialog" && nameOf(element) === name,
		choice:
			(name: string, _chosen: boolean): Read =>
			(element) =>
				element.role === "AXButton" && nameOf(element) === name,
		toggle:
			(name: string, on: boolean): Read =>
			(element) =>
				element.role === "AXCheckBox" &&
				nameOf(element) === name &&
				element.value === (on ? "1" : "0"),
		indicator:
			(name: string): Read =>
			(element) =>
				element.role === "AXGroup" && nameOf(element) === name,
		pin: (): Read => (element) =>
			element.role === "AXTextField" &&
			element.subrole === "AXSecureTextField" &&
			nameOf(element) === "PIN",
	},
	windows: {
		row:
			({ name, place, tabs, expanded }: Row): Read =>
			(element) =>
				element.role === "TreeItem" &&
				element.title === `${name}, ${tabsOf(tabs)}` &&
				element.level === 1 &&
				element.position === place &&
				element.expanded === (expanded ? "expanded" : "collapsed"),
		tab:
			(title: string, place: string, shown: boolean): Read =>
			(element) =>
				element.role === "TreeItem" &&
				element.title === title &&
				element.level === 2 &&
				element.position === place &&
				element.selected === shown,
		splitter:
			(width: number): Read =>
			(element) =>
				element.role === "Slider" &&
				element.title === "Sidebar width" &&
				element.value === String(width),
		settings:
			(on: boolean): Read =>
			(element) =>
				element.role === "Button" &&
				element.title === "Settings" &&
				element.toggle === (on ? "on" : "off"),
		heading:
			(name: string, level: number): Read =>
			(element) =>
				element.title === name && element.headingLevel === level,
		picker:
			(name: string, value: string): Read =>
			(element) =>
				element.role === "ComboBox" &&
				element.title === name &&
				element.value === value &&
				element.expanded === "collapsed",
		dialog:
			(name: string): Read =>
			(element) =>
				element.dialog === true && element.title === name,
		choice:
			(name: string, chosen: boolean): Read =>
			(element) =>
				element.role === "RadioButton" &&
				element.title === name &&
				element.selected === chosen &&
				/^\d+ of \d+$/.test(element.position ?? ""),
		toggle:
			(name: string, on: boolean): Read =>
			(element) =>
				element.role === "Button" &&
				element.title === name &&
				element.toggle === (on ? "on" : "off"),
		indicator:
			(name: string): Read =>
			(element) =>
				element.role === "Edit" &&
				element.title === "PIN" &&
				element.description === name,
		pin: (): Read => (element) =>
			element.role === "Edit" &&
			element.title === "PIN" &&
			element.protected === true,
	},
});

// Quit Pane?'s title, each system's word for quitting.
const QUIT = bySystem({ mac: "Quit Pane?", windows: "Exit Pane?" });

// An element as one line, with what each system adds to its role and name.
const describe = ({ depth, rect, ...element }: AccessibleElement) =>
	`${"  ".repeat(Math.min(depth, 20))}${JSON.stringify(element)}`;

// What a screen reader reads of Pane's own views, from the system's own
// accessibility tree, as VoiceOver and Narrator reach it (ADR 0071): the
// sidebar's profiles and tabs, each at its place, the open one's tabs shown,
// the shown tab selected; the sidebar's edge at its width; the Settings
// button on while Settings shows; Settings' headings and its pickers' choices;
// the App icon dialog and its chosen sky; the profile sheet, its chosen
// color and platform, the proxy's fields only while the proxy is on, and its
// switch on after the pointer leaves it (Views' Button cleared a switch's
// state at each change of its own); Quit Pane? and an extension's prompt as
// dialogs named by their titles; and the PIN's dots by their count. The Mac's read
// as ADR 0071 has them since, Windows' as commit 490757e made them for
// Narrator.
const screenReader: Scenario = async (context) => {
	if (!(await canReadUI())) {
		context.defer(
			"what a screen reader reads of Pane's views (the screen locked)",
		);

		return;
	}

	const app = await launchPane(context, {
		args: [`--load-extension=${PROBE}`],
	});

	const pid = app.browser.pid;
	const read = () => accessibilityTree(pid);

	// Waits for the tree to read `reads` of some element, else fails with
	// what it reads.
	const expectRead = async (what: string, reads: Read) => {
		const tree = await pollValue(
			read,
			(elements) => elements.some(reads),
			10_000,
			250,
		);

		expect(
			tree.some(reads),
			`${what} isn't read so; the tree:\n${tree.map(describe).join("\n")}`,
		);
	};

	const expectNoName = async (what: string, name: string) => {
		const tree = await read();

		expect(
			!tree.some((element) => nameOf(element) === name),
			`${what}: "${name}" is read:\n${tree.map(describe).join("\n")}`,
		);
	};

	const work = await app.createProfile("Work", "BLUE");
	const home = await app.createProfile("Home", "ROSE");

	await app.openPage(work, context.fixtures.url("light.html?screen-reader"));

	const { tab } = await app.openPage(
		work,
		context.fixtures.url("nav-a.html?screen-reader"),
	);

	const rest = await app.waitFor(
		"the second tab shown, its title in",
		(state) =>
			state.activeTabID === tab &&
			(state.profiles.find((profile) => profile.id === work)?.tabs ?? []).every(
				(each) => each.title !== "",
			),
	);

	// The sidebar: each profile at its place, the open one's tabs, the shown
	// tab selected.
	const profiles = rest.profiles.filter((profile) =>
		[work, home].includes(profile.id),
	);

	for (const [index, profile] of profiles.entries()) {
		await expectRead(
			`${profile.name}'s row`,
			READS.row({
				name: profile.name,
				place: `${index + 1} of ${rest.profiles.length}`,
				tabs: profile.tabs.length,
				expanded: profile.expanded,
			}),
		);
	}

	const tabs = rest.profiles.find((profile) => profile.id === work)?.tabs ?? [];

	for (const [index, each] of tabs.entries()) {
		await expectRead(
			`the tab ${each.title}`,
			READS.tab(each.title, `${index + 1} of ${tabs.length}`, each.id === tab),
		);
	}

	await expectRead(
		"the sidebar's edge",
		READS.splitter(Math.round(rest.sidebarWidth)),
	);

	// The edge resized by its keys once a screen reader has focused it, a
	// tenth of its range a step, as Views' sliders step: Right and Up wider,
	// Left and Down narrower (until commit f475f04 no key moved it on Windows).
	const edgeWidth = Math.round(rest.sidebarWidth);
	await app.send("screenReaderFocus", { name: "pane.sidebar.resize" });

	for (const [key, change] of [
		["right", 20],
		["up", 20],
		["left", -20],
		["down", -20],
	] as [string, number][]) {
		const before = Math.round((await app.state()).sidebarWidth);
		await app.send("dispatchKey", { key });

		await app.waitFor(
			`the edge's ${key} key`,
			(state: AppState) => Math.round(state.sidebarWidth) === before + change,
		);
	}

	await expectRead("the sidebar's edge, back", READS.splitter(edgeWidth));

	await expectRead("Settings, its page hidden", READS.settings(false));

	// Settings: the button on, its headings, and the Theme picker's choice.
	await app.send("showPage", { page: "settings" });
	await app.waitFor("Settings", (state: AppState) => state.page === "settings");
	await expectRead("Settings, its page shown", READS.settings(true));
	await expectRead("Settings' title", READS.heading("Settings", 1));
	await expectRead("Appearance", READS.heading("Appearance", 2));
	await expectRead("the Theme picker", READS.picker("Theme", "System"));

	// The App icon dialog, named, its chosen sky chosen.
	await app.send("appIconPicker", { open: true });
	await app.waitFor("the App icon picker", (state) => state.appIconPicker);
	await expectRead("the App icon dialog", READS.dialog("App icon"));
	await expectRead("the chosen sky", READS.choice("Automatic", true));
	await expectRead("another sky", READS.choice("Day", false));
	await app.send("appIconPicker", { open: false });
	await app.waitFor("the picker closed", (state) => !state.appIconPicker);

	await app.send("showPage", { page: "browser" });
	await app.waitFor("the browser", (state) => state.page === "browser");

	// The profile sheet: its color and platform chosen, the proxy's fields
	// read only once it is on, and its switch on still once the pointer has
	// left it.
	await app.send("openProfileSheet", { profile: work });

	await app.waitFor(
		"Work's sheet",
		(state) =>
			state.profileSheet === work && state.focus === "pane.profile.name",
	);

	// Once it has slid in (0.2 s).
	await Bun.sleep(300);

	await expectRead("the sheet", READS.dialog("Edit profile"));
	await expectRead("the sheet's title", READS.heading("Edit profile", 1));
	await expectRead("Work's color", READS.choice("Blue", true));
	await expectRead("another color", READS.choice("Rose", false));
	await expectRead("the proxy off", READS.toggle("Proxy", false));
	await expectNoName("the proxy off", "Proxy host");

	const [x = 0, y = 0, width = 0, height = 0] =
		(await app.frames("pane.profile.proxy"))["pane.profile.proxy"] ?? [];

	expect(width > 0 && height > 0, "the sheet shows no Proxy switch");
	await app.send("hoverWindow", { x: x + width / 2, y: y + height / 2 });
	await app.click("pane.profile.proxy");
	await expectRead("the proxy on", READS.toggle("Proxy", true));

	await expectRead(
		"the proxy's host",
		(element) => nameOf(element) === "Proxy host",
	);

	// The pointer leaves the switch, which goes from hovered to normal.
	const [sheetX = 0, sheetY = 0] =
		(await app.frames("pane.profileSheet"))["pane.profileSheet"] ?? [];

	await app.send("hoverWindow", { x: sheetX + 32, y: sheetY + 28 });
	await Bun.sleep(300);

	await expectRead(
		"the proxy on, the pointer gone",
		READS.toggle("Proxy", true),
	);

	await app.click("pane.profile.cancel");
	await app.waitFor("the sheet closed", (state) => state.profileSheet === null);

	// Quit Pane?, named by its title, then cancelled.
	await app.send("menu", { path: ["Pane", `${QUIT_WORD} Pane`] });
	await app.waitFor("Quit Pane?", (state) => state.quitConfirmation);
	await expectRead("Quit Pane?", READS.dialog(QUIT));
	await app.send("dispatchKey", { key: "\u001b" });
	await app.waitFor("Quit Pane? gone", (state) => !state.quitConfirmation);

	// An extension's prompt, the dialog named by its title.
	const id = await extensionId(app, work, NAME);

	await app.send("askToAddExtension", { id, profile: work });

	const asked = await waitForSheet(app, "extensionInstall");

	await expectRead("the extension's prompt", READS.dialog(asked.title ?? ""));

	// Where Return presses the button with the ring (Windows, ADR 0126), the
	// prompt's Cancel takes the keys and Return presses it: Enter on a focused
	// Cancel added the extension (until commit f475f04). In front Tab gives it
	// the ring, by the window's own traversal; behind, a screen reader's focus
	// does, as the channel's keys reach the window past its traversal (a Tab
	// handed so moved nothing, and Return accepted). Elsewhere Esc cancels it.
	if (RETURN_PRESSES_THE_RING) {
		if (context.foreground) {
			await pressWindowKey(context, app, "\t");
		} else {
			await app.send("screenReaderFocus", {
				name: "pane.extensionPopup.cancel",
			});
		}

		await app.waitFor(
			"the prompt's Cancel with the keys",
			(state) => state.focus === "pane.extensionPopup.cancel",
		);

		await pressWindowKey(context, app, "\r");
	} else {
		await app.send("dispatchKey", { key: "\u001b" });
	}

	await app.waitFor("the prompt gone", (state) => state.sheet === null);

	const answer = (await app.send<string | null>("promptAnswer")).result;
	expect(answer === "cancelled", `the prompt was answered ${answer}`);

	// The PIN's dots, as Set up PIN shows them, read as text; the screen a
	// dialog named by its step's title, the keys in a protected field, so a
	// screen reader never says a digit typed (Narrator said "9 9 9 9" until
	// commit f475f04).
	await app.send("showPinScreen", { mode: "setup" });

	await app.waitFor(
		"Set up PIN",
		(state) => state.security.screen?.mode === "setup",
	);

	await expectRead("the empty dots", READS.indicator("0 of 4 digits"));
	await expectRead("Set up PIN's dialog", READS.dialog("Enter new PIN"));
	await expectRead("the PIN's field", READS.pin());
	await app.send("pinKeys", { keys: "12" });
	await expectRead("two dots", READS.indicator("2 of 4 digits"));
	await app.send("pinKeys", { keys: "34>" });

	await app.waitFor(
		"the PIN asked again",
		(state) => state.security.screen?.step === "confirm",
	);

	await expectRead("the next step's dialog", READS.dialog("Confirm PIN"));
	await app.send("pinKeys", { keys: "1234" });

	await app.waitFor(
		"the PIN set up",
		(state) => state.security.screen === null && state.security.pinLength === 4,
	);

	// The Lock, which shows no title: its dialog by what it is, the keys in
	// the protected field.
	await app.send("lockNow");
	await app.waitFor("locked", (state) => state.security.locked);
	await expectRead("the Lock's dialog", READS.dialog("Pane is locked"));
	await expectRead("the Lock's PIN field", READS.pin());
	await app.send("pinKeys", { keys: "1234" });
	await app.waitFor("unlocked", (state) => !state.security.locked, 60_000);
};

export default screenReader;
