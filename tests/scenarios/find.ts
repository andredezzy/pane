import { launchPane } from "../lib/pane";
import { PRIMARY } from "../lib/platform";
import { expect, type Scenario } from "../run";

// Find in page through the app: counts, stepping forward and back, closing,
// and Find Next opening a closed bar on the last search.
const find: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Find");
	const url = context.fixtures.url("find.html");
	await app.openPage(profile, url);

	await app.send("find", { text: "needle" });

	await app.waitFor(
		"5 matches",
		(state) => state.find.matches === 5 && state.find.activeMatch === 1,
	);

	await app.send("find", { text: "needle" });

	await app.waitFor("match 2", (state) => state.find.activeMatch === 2);

	await app.send("find", { text: "needle", forward: false });

	await app.waitFor("match 1", (state) => state.find.activeMatch === 1);

	// ⌘G and ⇧⌘G, the menu bar's keys, step as Return and ⇧Return do.
	await app.send("key", { key: "g", modifiers: [PRIMARY] });

	await app.waitFor("⌘G", (state) => state.find.activeMatch === 2);

	await app.send("key", { key: "g", modifiers: [PRIMARY, "shift"] });

	await app.waitFor("⇧⌘G", (state) => state.find.activeMatch === 1);

	await app.send("find", { text: "haystack" });

	await app.waitFor("1 match", (state) => state.find.matches === 1);

	await app.send("find", { text: "absent-word" });

	await app.waitFor("no match", (state) => state.find.matches === 0);

	const { state: closed } = await app.send("closeFind");

	expect(
		!closed.find.visible && closed.find.text === "",
		"the find bar stayed open",
	);

	// ⌘G and ⇧⌘G with the bar closed open it on the last search and find it,
	// as Chrome's do (W9a: they opened nothing); ⌘F still opens it empty
	// (ADR 0067).
	await app.send("find", { text: "needle" });
	await app.waitFor("5 matches again", (state) => state.find.matches === 5);

	for (const modifiers of [[PRIMARY], [PRIMARY, "shift"]]) {
		await app.send("closeFind");
		await app.send("key", { key: "g", modifiers });

		await app.waitFor(
			`${modifiers.length > 1 ? "⇧⌘G" : "⌘G"} on the closed bar`,
			(state) =>
				state.find.visible &&
				state.find.text === "needle" &&
				state.find.matches === 5 &&
				state.find.activeMatch > 0,
		);
	}

	await app.send("closeFind");
	await app.send("key", { key: "f", modifiers: [PRIMARY] });

	await app.waitFor(
		"⌘F opening it empty",
		(state) => state.find.visible && state.find.text === "",
	);
};

export default find;
