import { join } from "node:path";
import { workerPage } from "../lib/extensions";
import { poll } from "../lib/poll";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

const PROBE = join(import.meta.dir, "..", "pages", "extensions", "commands");
const NAME = "Pane commands probe";

type Command = { name: string; tabId: number | null };

// An extension's keyboard shortcuts (chrome.commands) work as in Chrome:
// with a page focused, the probe's ⌘⇧Y runs its command (onCommand) for
// the active tab, and ⌘⇧U its action (_execute_action, onClicked); with
// the address field being edited, ⌘⇧Y runs its command too.
const extensionShortcuts: Scenario = async (context) => {
	const { app } = await launchTraced(context, "extension-shortcuts", {
		args: [`--load-extension=${PROBE}`],
	});

	const profile = await app.createProfile("Shortcuts");
	const url = context.fixtures.url("light.html?extension-shortcuts");
	const tab = await app.openTab(profile, url);
	await app.page(tab, url);

	const loaded = await app.waitFor("the probe extension", (state) =>
		(state.extensions[profile] ?? []).some((item) => item.name === NAME),
	);

	const id = (loaded.extensions[profile] ?? []).find(
		(item) => item.name === NAME,
	)?.id as string;

	const worker = await workerPage(app, id, url);

	const commands = (count: number) =>
		poll(
			`${count} commands in the probe`,
			async () => {
				const kept = await worker.evaluate<Command[]>(
					`chrome.storage.session.get("commands").then((stored) => stored.commands ?? [])`,
				);

				return kept.length >= count && kept;
			},
			5_000,
		);

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

	return {
		extension: id,
		commands: await commands(3),
		fieldFocus: editing.focus,
	};
};

export default extensionShortcuts;
