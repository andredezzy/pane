import { join } from "node:path";
import {
	EXTENSIONS_PAGE,
	extensionsPage,
	press,
	SHORTCUTS,
	SHORTCUTS_PAGE,
	SIDEBAR,
	typeKey,
} from "../lib/chrome-extensions";
import { extensionId, workerPage } from "../lib/extensions";
import { launchPane } from "../lib/pane";
import { poll } from "../lib/poll";
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

// One command as Chrome's shortcuts page lists it: its description, and its
// key as the page writes it on the Mac ("⇧⌘Y"; empty when none).
type Listed = {
	extension: string;
	commands: { description: string; shortcut: string }[];
};

// An extension's keyboard shortcuts (chrome.commands) work as in Chrome
// (ADR 0098): with a page focused, the probe's ⌘⇧Y runs its command
// (onCommand) for the active tab, and ⌘⇧U its action (_execute_action,
// onClicked); with the address field being edited, ⌘⇧Y runs its command
// too. They change on Chrome's own shortcuts page in the profile's tab
// (chrome://extensions/shortcuts, D3, Paper 10.3), reached from the
// extensions page's Keyboard shortcuts: it lists the probes' commands with
// their keys; a key typed into a field sets it, and only the new key runs
// the command; ⌥⇧K, the second probe's, typed into the probe's field while
// it records moves there, runs nothing as it is typed (Chrome's page
// suspends the shortcuts, Pane's window's with them), and from then on runs
// the probe's command and not the second's; Clear empties one; and ⌘T, a
// key of Pane's menu bar, typed there, is refused: the field gives back the
// key it had, or none, and ⌘T stays Pane's in the window, where it opens a
// tab and runs no extension's command. Keys on a page and in the address
// go to Pane's window as the system hands them over, in the background too;
// keys and clicks on Chrome's page go through its input path (CDP's Input),
// which Pane's window sees first.
const extensionShortcuts: Scenario = async (context) => {
	const app = await launchPane(context, {
		args: [`--load-extension=${PROBE},${SECOND}`],
	});

	const profile = await app.createProfile("Shortcuts");
	const url = context.fixtures.url("light.html?extension-shortcuts");
	const { tab } = await app.openPage(profile, url);

	const id = await extensionId(app, profile, NAME);
	const secondID = await extensionId(app, profile, SECOND_NAME);

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

	// A chord on the profile's page, as typed.
	const onPage = async (key: string, modifiers: string[]) => {
		await app.send("select", { tab });
		await app.send("typeInPage", { key, modifiers });
	};

	await onPage("y", ["command", "shift"]);
	const [command] = await commands(1);

	expect(
		command?.name === "probe-command" && typeof command.tabId === "number",
		`⌘⇧Y ran ${JSON.stringify(command)}`,
	);

	await onPage("u", ["command", "shift"]);
	const action = (await commands(2))[1];

	expect(
		action?.name === "_execute_action" && action.tabId === command.tabId,
		`⌘⇧U ran ${JSON.stringify(action)}`,
	);

	// With the address field being edited, as with the page focused: a
	// click on it, as ⌘L needs the front (shortcuts' own deferral).
	await app.click("pane.toolbar.address");

	await app.waitFor(
		"the address field focused",
		(state) => state.focus === "pane.toolbar.address",
	);

	await app.send("windowKey", { key: "y", modifiers: ["command", "shift"] });
	const fromField = (await commands(3))[2];

	expect(
		fromField?.name === "probe-command",
		`⌘⇧Y in the address field ran ${JSON.stringify(fromField)}`,
	);

	// Chrome's shortcuts page, from the extensions page's Keyboard shortcuts.
	await app.send("select", { tab });
	await app.send("extensionsMenu", { control: "puzzle" });
	await app.waitFor("the extensions menu", (state) => state.extensionsMenu);
	await app.send("extensionsMenu", { control: "manage" });
	const { page } = await extensionsPage(app, profile, EXTENSIONS_PAGE);
	const shortcutsTab = (await app.state()).activeTabID as string;

	await press(
		page,
		`${SIDEBAR}.querySelector("#sectionsShortcuts")`,
		"Keyboard shortcuts",
	);

	await poll(
		"the shortcuts page",
		() =>
			page
				.evaluate<boolean>(
					`location.href === ${JSON.stringify(SHORTCUTS_PAGE)} && !!${SHORTCUTS}?.querySelector(".shortcut-card")`,
				)
				.catch(() => false),
		10_000,
	);

	const listedNow = () =>
		page.evaluate<Listed[]>(
			`[...${SHORTCUTS}.querySelectorAll(".shortcut-card")].map((card) => ({
				extension: card.querySelector('[role="heading"]').textContent.trim(),
				commands: [...card.querySelectorAll(".command-entry")].map((entry) => ({
					description: entry.querySelector(".command-name").textContent.trim(),
					shortcut: entry.querySelector("cr-shortcut-input").shortcut ?? "",
				})),
			}))`,
		);

	const commandsOf = (all: Listed[], name: string) =>
		all.find((each) => each.extension === name)?.commands ?? [];

	const listed = await listedNow();

	expect(
		JSON.stringify(commandsOf(listed, NAME)) ===
			JSON.stringify([
				{ description: "Activate the extension", shortcut: "⇧⌘U" },
				{ description: "Run the probe's command", shortcut: "⇧⌘Y" },
			]) &&
			JSON.stringify(commandsOf(listed, SECOND_NAME)) ===
				JSON.stringify([
					{
						description: "Run the second probe's command",
						shortcut: "⌥⇧K",
					},
				]),
		`the shortcuts page lists ${JSON.stringify(listed)}`,
	);

	// A command's field on the page, by its extension and description.
	const field = (extension: string, description: string) =>
		`[...${SHORTCUTS}.querySelectorAll(".shortcut-card")]
			.find((card) => card.querySelector('[role="heading"]').textContent.trim() === ${JSON.stringify(extension)})
			.querySelectorAll(".command-entry")[
				[...[...${SHORTCUTS}.querySelectorAll(".shortcut-card")]
					.find((card) => card.querySelector('[role="heading"]').textContent.trim() === ${JSON.stringify(extension)})
					.querySelectorAll(".command-name")]
					.findIndex((name) => name.textContent.trim() === ${JSON.stringify(description)})
			].querySelector("cr-shortcut-input")`;

	const shortcutOf = async (extension: string, description: string) =>
		page.evaluate<string>(`${field(extension, description)}.shortcut ?? ""`);

	const waitShortcut = (
		extension: string,
		description: string,
		wanted: string,
	) =>
		poll(
			`${description} at ${wanted || "none"}`,
			async () => (await shortcutOf(extension, description)) === wanted,
			5_000,
		);

	// The field's pencil starts a recording; the page then takes the keys.
	const record = async (extension: string, description: string) => {
		await press(
			page,
			`${field(extension, description)}.shadowRoot.querySelector("#edit")`,
			`${description}'s pencil`,
		);

		await poll(
			`${description} recording`,
			() =>
				page.evaluate<boolean>(
					`!${field(extension, description)}.shadowRoot.querySelector("#input").readonly`,
				),
			5_000,
		);

		// The page asks the browser to suspend the shortcuts as it starts
		// recording (developerPrivate.setShortcutHandlingSuspended), a call
		// that lands a moment later.
		await Bun.sleep(300);
	};

	// ⌃⇧Y for the probe's command: the new key runs it, the old one nothing.
	await record(NAME, "Run the probe's command");
	await typeKey(page, "y", ["control", "shift"]);
	await waitShortcut(NAME, "Run the probe's command", "⌃⇧Y");

	await onPage("y", ["control", "shift"]);
	await commands(4);
	await onPage("y", ["command", "shift"]);
	await noMore(worker, 4, "⌘⇧Y, no longer the probe's key, still ran");

	// ⌥⇧K, the second probe's, typed into the probe's field while it
	// records: it runs nothing as it is typed, and moves here.
	await app.send("select", { tab: shortcutsTab });
	const secondBefore = (await kept(secondWorker)).length;
	await record(NAME, "Run the probe's command");
	await typeKey(page, "k", ["option", "shift"]);
	await waitShortcut(NAME, "Run the probe's command", "⌥⇧K");
	await waitShortcut(SECOND_NAME, "Run the second probe's command", "");

	await noMore(
		secondWorker,
		secondBefore,
		"⌥⇧K ran the second probe's command as it was typed",
	);

	await onPage("k", ["option", "shift"]);
	await commands(5);

	await noMore(
		secondWorker,
		secondBefore,
		"⌥⇧K ran the second probe's command",
	);

	// Clear empties the action's: ⌘⇧U runs nothing.
	await app.send("select", { tab: shortcutsTab });

	await press(
		page,
		`${field(NAME, "Activate the extension")}.shadowRoot.querySelector("#clear")`,
		"the action's Clear",
	);

	await waitShortcut(NAME, "Activate the extension", "");
	await onPage("u", ["command", "shift"]);
	await noMore(worker, 5, "the cleared ⌘⇧U still ran");

	// ⌘T, a key of Pane's menu bar, typed into a field as it records: the
	// page takes it, no tab opens, and Pane refuses it (ADR 0098, Paper
	// 10.3's note) with no message on the page: the field shows again the
	// key it had, or none, and Chrome's own record of the command holds no
	// ⌘T.
	await app.send("select", { tab: shortcutsTab });

	const tabsBefore =
		(await app.state()).profiles.find((each) => each.id === profile)?.tabs
			.length ?? 0;

	const recorded = (command: string) =>
		page.evaluate<string>(
			`chrome.developerPrivate.getExtensionInfo(${JSON.stringify(id)}).then((info) => info.commands.find((each) => each.name === ${JSON.stringify(command)})?.keybinding ?? "")`,
		);

	const refused = async (description: string, command: string, had: string) => {
		await record(NAME, description);
		await typeKey(page, "t", ["command"]);

		// The page took the key: the field records no more.
		await poll(
			`${description} done recording`,
			() =>
				page.evaluate<boolean>(
					`${field(NAME, description)}.shadowRoot.querySelector("#input").readonly`,
				),
			5_000,
		);

		await waitShortcut(NAME, description, had).catch(async (error: Error) => {
			throw new Error(
				`${error.message}: the field reads ${JSON.stringify(await shortcutOf(NAME, description))}, Chrome's record ${JSON.stringify(await recorded(command))}`,
			);
		});

		const kept = await recorded(command);

		expect(
			kept === had,
			`${description} holds ${JSON.stringify(kept)} after ⌘T, not ${JSON.stringify(had)}`,
		);
	};

	await refused("Activate the extension", "_execute_action", "");
	await refused("Run the probe's command", "probe-command", "⌥⇧K");

	const tabsWhileTyped =
		(await app.state()).profiles.find((each) => each.id === profile)?.tabs
			.length ?? 0;

	expect(tabsWhileTyped === tabsBefore, "⌘T typed into the page opened a tab");

	// The key given back still runs its command.
	await onPage("k", ["option", "shift"]);
	await commands(6);

	// On the page, ⌘T stays Pane's: the action does not run. The tab it
	// opens comes from the menu bar once the page has left the key, which
	// takes Pane in front (shortcuts' own deferral).
	if (context.foreground) {
		await app.send("select", { tab });
		await app.send("activate");
		await app.waitFor("the key window", (state) => state.window.key, 10_000);
		await app.send("key", { key: "t", modifiers: ["command"] });

		await app.waitFor(
			"⌘T's new tab",
			(state) =>
				(state.profiles.find((each) => each.id === profile)?.tabs.length ?? 0) >
				tabsBefore,
		);
	} else {
		await onPage("t", ["command"]);

		context.defer(
			"⌘T opening a tab once the page leaves it: the menu bar's key needs Pane in front",
		);
	}

	await noMore(worker, 6, "⌘T ran the probe's action");

	return { listed };
};

export default extensionShortcuts;
