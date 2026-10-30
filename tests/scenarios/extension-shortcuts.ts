import { join } from "node:path";
import { isScreenLocked, mainWindowOf, readControl } from "../lib/cua";
import { workerPage } from "../lib/extensions";
import type { AppState, ExtensionCommands } from "../lib/pane";
import { poll } from "../lib/poll";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

const PROBE = join(import.meta.dir, "..", "pages", "extensions", "commands");

const SECOND = join(
	import.meta.dir,
	"..",
	"pages",
	"extensions",
	"commands-second",
);

const NAME = "Pane commands probe";
const SECOND_NAME = "Pane second commands probe";

type Command = { name: string; tabId: number | null };

// An extension's keyboard shortcuts (chrome.commands) work as in Chrome:
// with a page focused, the probe's ⌘⇧Y runs its command (onCommand) for
// the active tab, and ⌘⇧U its action (_execute_action, onClicked); with
// the address field being edited, ⌘⇧Y runs its command too.
//
// The Extensions page's Keyboard shortcuts (ADR 0104) lists them as
// chrome://extensions/shortcuts does, and changes them through the dialog's
// field, with keys typed as the user types them: a new key fires and the old
// one no longer does; Chrome's refusals and a key of the menu bar's leave a
// message, the field still recording; a key another extension had moves
// here and says so; Clear empties one; and while a field records, no
// shortcut runs.
const extensionShortcuts: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "extension-shortcuts", {
		args: [`--load-extension=${PROBE},${SECOND}`],
	});

	const profile = await app.createProfile("Shortcuts");
	const url = context.fixtures.url("light.html?extension-shortcuts");
	const tab = await app.openTab(profile, url);
	await app.page(tab, url);

	const loaded = await app.waitFor("the probe extensions", (state) =>
		[NAME, SECOND_NAME].every((name) =>
			(state.extensions[profile] ?? []).some((item) => item.name === name),
		),
	);

	const idOf = (name: string) =>
		(loaded.extensions[profile] ?? []).find((item) => item.name === name)
			?.id as string;

	const id = idOf(NAME);
	const secondID = idOf(SECOND_NAME);

	const worker = await workerPage(app, id, url);
	const secondWorker = await workerPage(app, secondID, url);

	const kept = (page: typeof worker) =>
		page.evaluate<Command[]>(
			`chrome.storage.session.get("commands").then((stored) => stored.commands ?? [])`,
		);

	const commands = (count: number) =>
		poll(
			`${count} commands in the probe`,
			async () => {
				const all = await kept(worker);

				return all.length >= count && all;
			},
			5_000,
		);

	// No more than `count` commands reach the probe in the time one takes.
	const noMore = async (page: typeof worker, count: number, what: string) => {
		await Bun.sleep(700);
		const all = await kept(page);

		expect(all.length === count, `${what}: ${JSON.stringify(all)}`);
	};

	await app.send("key", { key: "y", modifiers: ["command", "shift"] });
	const [command] = await commands(1);

	expect(
		command?.name === "probe-command" && typeof command.tabId === "number",
		`⌘⇧Y ran ${JSON.stringify(command)}`,
	);

	await app.send("key", { key: "u", modifiers: ["command", "shift"] });
	const action = (await commands(2))[1];

	expect(
		action?.name === "_execute_action" && action.tabId === command.tabId,
		`⌘⇧U ran ${JSON.stringify(action)}`,
	);

	// With the address field being edited, as with the page focused.
	await app.send("key", { key: "l", modifiers: ["command"] });

	const editing = await app.waitFor(
		"the address field focused",
		(state) => state.focus === "field: Search or enter URL",
	);

	await app.send("key", { key: "y", modifiers: ["command", "shift"] });
	const fromField = (await commands(3))[2];

	expect(
		fromField?.name === "probe-command",
		`⌘⇧Y in the address field ran ${JSON.stringify(fromField)}`,
	);

	await app.send("releaseModifiers");

	// What the dialog lists, read through the same controller method.
	const { result: listed } = await app.send<ExtensionCommands[]>(
		"extensionShortcuts",
		{ profile },
	);

	// In one order of keys, to compare.
	const commandsOf = (all: ExtensionCommands[], extension: string) =>
		(all.find((entry) => entry.id === extension)?.commands ?? []).map(
			({ name, description, shortcut, glyphs }) => ({
				name,
				description,
				shortcut,
				glyphs,
			}),
		);

	expect(
		JSON.stringify(commandsOf(listed, id)) ===
			JSON.stringify([
				{
					name: "_execute_action",
					description: "Activate the extension",
					shortcut: "Command+Shift+U",
					glyphs: "⇧⌘U",
				},
				{
					name: "probe-command",
					description: "Run the probe's command",
					shortcut: "Command+Shift+Y",
					glyphs: "⇧⌘Y",
				},
			]) &&
			JSON.stringify(commandsOf(listed, secondID)) ===
				JSON.stringify([
					{
						name: "probe-second-command",
						description: "Run the second probe's command",
						shortcut: "Alt+Shift+K",
						glyphs: "⌥⇧K",
					},
				]),
		`the dialog lists ${JSON.stringify(listed)}`,
	);

	const shortcutOf = (state: AppState, extension: string, name: string) =>
		commandsOf(state.shortcutsDialog?.extensions ?? [], extension).find(
			(item) => item.name === name,
		)?.shortcut;

	const render = async (name: string) => {
		// Past the hover's and the panel's 0.15 s.
		await Bun.sleep(300);

		await app.send("render", {
			path: join(context.out, `extension-shortcuts-${name}.png`),
		});
	};

	// The header's Keyboard shortcuts, for the profile the page lists.
	await app.send("showPage", { page: "extensions" });
	await app.send("extensionShortcutsDialog", { control: "open", profile });

	await app.waitFor(
		"the Keyboard shortcuts dialog",
		(state) => state.shortcutsDialog?.profile === profile,
	);

	await render("light");
	await app.send("theme", { theme: "dark" });
	await render("dark");
	await app.send("theme", { theme: "light" });

	const record = async (extension: string, name: string) => {
		await app.send("extensionShortcutsDialog", {
			control: "record",
			id: extension,
			name,
		});

		await app.waitFor(
			`${name} recording`,
			(state) => state.shortcutsDialog?.recording?.command === name,
		);
	};

	// A real click on a field, in Pane's own window: pressed, the foreground
	// at 6%; let go, it records. Where the field is, accessibility reads,
	// which needs an unlocked screen. (Its hover needs the real pointer: a
	// posted move reaches no tracking area.)
	if (await isScreenLocked()) {
		context.defer("a click's press and recording: the screen is locked");
		await record(id, "probe-command");
	} else {
		const pid = app.browser.pid;

		const frame = await readControl(
			pid,
			await mainWindowOf(pid),
			"Run the probe's command shortcut",
			/AXButton/,
		);

		const field = { x: frame.x + frame.w / 2, y: frame.y + frame.h / 2 };

		await app.send("pointerWindow", { phase: "down", ...field });
		await render("pressed");
		await app.send("pointerWindow", { phase: "up", ...field });

		await app.waitFor(
			"recording from a click",
			(state) => state.shortcutsDialog?.recording?.command === "probe-command",
		);
	}

	// Recording: Type a shortcut, then the modifiers as they go down.
	await render("recording");

	await app.send("holdModifiers", { modifiers: ["option", "command"] });

	await app.waitFor("⌥⌘ held", (state) => state.shortcutsDialog?.held === "⌥⌘");

	await render("held");
	await app.send("releaseModifiers");

	// Each refusal leaves its message, and the field recording.
	const tabCount = (state: AppState) =>
		state.profiles.find((item) => item.id === profile)?.tabs.length;

	const tabs = tabCount(await app.state());

	const refuse = async (
		key: string,
		modifiers: string[],
		message: string,
		shot: string,
	) => {
		await app.send("key", { key, modifiers });

		const state = await app.waitFor(
			`refused with ${message}`,
			(current) => current.shortcutsDialog?.message === message,
		);

		expect(
			state.shortcutsDialog?.recording?.command === "probe-command" &&
				shortcutOf(state, id, "probe-command") === "Command+Shift+Y",
			`after ${message}, still recording, the shortcut kept`,
		);

		await app.send("releaseModifiers");
		await render(shot);
	};

	await refuse("y", [], "Include Ctrl, Alt, or ⌘", "no-modifier");
	await refuse("y", ["option", "command"], "Invalid combination", "too-many");
	await refuse("-", ["command"], "Type a letter", "no-key");
	await refuse("t", ["command"], "⌘T is Pane’s New Tab", "panes-key");

	await app.send("theme", { theme: "dark" });
	await render("panes-key-dark");
	await app.send("theme", { theme: "light" });

	expect(tabCount(await app.state()) === tabs, "⌘T, recorded, opened no tab");

	// A new key: ⌃⇧Y.
	await app.send("key", { key: "y", modifiers: ["control", "shift"] });
	await app.send("releaseModifiers");

	await app.waitFor(
		"⌃⇧Y set",
		(state) =>
			state.shortcutsDialog?.recording === null &&
			shortcutOf(state, id, "probe-command") === "Ctrl+Shift+Y",
	);

	await render("set");

	// A key another extension had moves here, and says where from.
	await record(id, "probe-command");
	await app.send("key", { key: "k", modifiers: ["option", "shift"] });
	await app.send("releaseModifiers");

	const moved = await app.waitFor(
		"⌥⇧K moved from the second probe",
		(state) =>
			shortcutOf(state, id, "probe-command") === "Alt+Shift+K" &&
			shortcutOf(state, secondID, "probe-second-command") === "",
	);

	expect(
		moved.shortcutsDialog?.moved?.from === SECOND_NAME &&
			moved.shortcutsDialog.moved.command === "probe-command",
		`the move reads ${JSON.stringify(moved.shortcutsDialog?.moved)}`,
	);

	await render("moved");

	await app.send("theme", { theme: "dark" });
	await render("moved-dark");
	await app.send("theme", { theme: "light" });

	// Clear: the action's key goes.
	await app.send("extensionShortcutsDialog", {
		control: "clear",
		id,
		name: "_execute_action",
	});

	await app.waitFor(
		"the action's shortcut cleared",
		(state) => shortcutOf(state, id, "_execute_action") === "",
	);

	// Esc stops a recording, then closes the dialog.
	await record(id, "_execute_action");
	await app.send("key", { key: "\u{1b}", modifiers: [] });

	const stopped = await app.waitFor(
		"the recording stopped",
		(state) => state.shortcutsDialog?.recording === null,
	);

	expect(
		stopped.shortcutsDialog !== null &&
			shortcutOf(stopped, id, "_execute_action") === "",
		"Esc stopped the recording, the dialog open",
	);

	await app.send("key", { key: "\u{1b}", modifiers: [] });

	await app.waitFor(
		"the dialog closed",
		(state) => state.shortcutsDialog === null,
	);

	// On the page: the new keys fire, the old ones no longer do.
	await app.send("showPage", { page: "browser" });
	await app.page(tab, url);

	await app.send("key", { key: "k", modifiers: ["option", "shift"] });
	const byNewKey = (await commands(4))[3];
	await app.send("releaseModifiers");

	expect(
		byNewKey?.name === "probe-command",
		`⌥⇧K ran ${JSON.stringify(byNewKey)}`,
	);

	await noMore(secondWorker, 0, "⌥⇧K no longer reaches the second probe");

	await app.send("key", { key: "y", modifiers: ["command", "shift"] });
	await app.send("key", { key: "y", modifiers: ["control", "shift"] });
	await app.send("key", { key: "u", modifiers: ["command", "shift"] });
	await app.send("releaseModifiers");
	await noMore(worker, 4, "⌘⇧Y, ⌃⇧Y, and the cleared ⌘⇧U run nothing");

	// While a field records, no shortcut runs: ⌥⇧K is typed into the second
	// probe's field, and moves back to it.
	await app.send("showPage", { page: "extensions" });
	await app.send("extensionShortcutsDialog", { control: "open", profile });
	await record(secondID, "probe-second-command");
	await app.send("key", { key: "k", modifiers: ["option", "shift"] });
	await app.send("releaseModifiers");

	const back = await app.waitFor(
		"⌥⇧K back on the second probe",
		(state) =>
			shortcutOf(state, secondID, "probe-second-command") === "Alt+Shift+K",
	);

	expect(
		back.shortcutsDialog?.moved?.from === NAME &&
			shortcutOf(back, id, "probe-command") === "",
		`the move back reads ${JSON.stringify(back.shortcutsDialog?.moved)}`,
	);

	await noMore(worker, 4, "⌥⇧K, recorded, ran nothing");
	await noMore(secondWorker, 0, "⌥⇧K, recorded, ran nothing");

	expect(
		has(
			"PaneBrowserContext.setExtensionShortcutsSuspended",
			(args) => args.suspended === true,
		),
		"recording paused the shortcuts",
	);

	await app.send("extensionShortcutsDialog", { control: "done" });

	return {
		extension: id,
		second: secondID,
		commands: await commands(4),
		fieldFocus: editing.focus,
		listed,
		moved: moved.shortcutsDialog?.moved,
		movedBack: back.shortcutsDialog?.moved,
	};
};

export default extensionShortcuts;
