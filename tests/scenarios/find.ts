import { launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

// Find in page through the app: counts, stepping forward and back, closing.
const find: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Find");
	const url = context.fixtures.url("find.html");
	const tab = await app.openTab(profile, url);
	await app.page(tab, url);

	await app.send("find", { text: "needle" });

	const first = await app.waitFor(
		"5 matches",
		(state) => state.find.matches === 5 && state.find.activeMatch === 1,
	);

	await app.send("find", { text: "needle" });

	const second = await app.waitFor(
		"match 2",
		(state) => state.find.activeMatch === 2,
	);

	await app.send("find", { text: "needle", forward: false });

	const backAgain = await app.waitFor(
		"match 1",
		(state) => state.find.activeMatch === 1,
	);

	await app.send("find", { text: "haystack" });

	const other = await app.waitFor(
		"1 match",
		(state) => state.find.matches === 1,
	);

	await app.send("find", { text: "absent-word" });

	const none = await app.waitFor(
		"no match",
		(state) => state.find.matches === 0,
	);

	const { state: closed } = await app.send("closeFind");

	expect(
		!closed.find.visible && closed.find.text === "",
		"the find bar stayed open",
	);

	return {
		first: first.find,
		second: second.find,
		backAgain: backAgain.find,
		other: other.find,
		none: none.find,
		closed: closed.find,
	};
};

export default find;
