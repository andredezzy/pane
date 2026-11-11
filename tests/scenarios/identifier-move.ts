import {
	existsSync,
	mkdirSync,
	readFileSync,
	realpathSync,
	writeFileSync,
} from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";
import { launchPane } from "../lib/pane";
import { expect, type Scenario, type ScenarioContext } from "../run";

// An earlier Pane's data, under its former identifier's folders
// (com.pane.desktop), moves to this one's (xyz.roundedcorners.pane) at launch,
// before anything reads it (ADR 0114): its profiles show, profiles.json is
// sealed (ADR 0113), and the former folders are gone. Pane derives the data
// root from a throwaway home of the scenario's own, as on a Mac; the user's
// folders are checked untouched. Once without a PIN, and once behind the PIN
// such a Pane kept as a bcrypt hash (0.13.1 and before, ADR 0122): Pane
// starts locked behind it, and the PIN opens the moved data.

// bcryptjs 3.0.3, hashSync("1234", 10), in the format 0.13.1 and the Electron
// app wrote it.
const FORMER_SECURITY = {
	state: {
		pin: {
			hash: "$2b$10$6RNvGASNquZ6f5LXjFap1eMcHOZTOzRYJVRe1DrjRG2juL90W5MNO",
			length: 4,
		},
		failedAttempts: 0,
	},
	version: 0,
};

async function move(context: ScenarioContext, pin: boolean) {
	const user = join(homedir(), "Library");

	const userFolders = () =>
		["Application Support", "Caches"].flatMap((folder) =>
			["com.pane.desktop", "xyz.roundedcorners.pane"].map((name) =>
				existsSync(join(user, folder, name)),
			),
		);

	const userBefore = userFolders();

	const home = realpathSync(context.tempDir("pane-identifier-"));
	const support = join(home, "Library", "Application Support");
	const caches = join(home, "Library", "Caches");
	const former = join(support, "com.pane.desktop");
	const dataDir = join(support, "xyz.roundedcorners.pane");
	const url = context.fixtures.url("smoke.html?moved");

	mkdirSync(former, { recursive: true });
	mkdirSync(join(caches, "com.pane.desktop"), { recursive: true });

	writeFileSync(
		join(former, "profiles.json"),
		JSON.stringify({
			state: {
				profiles: [
					{
						id: "6f9d2c1a-8b3e-4c7d-9a2f-1e5b7c3d9f02",
						name: "Moved",
						color: "BLUE",
						group: null,
						fingerprint: null,
						proxy: null,
						keepLoaded: false,
						tabs: [{ id: "moved-tab", url, title: "Moved", favicon: "" }],
						createdAt: "2026-10-30T00:00:00.000Z",
						updatedAt: "2026-10-30T00:00:00.000Z",
					},
				],
			},
			version: 0,
		}),
	);

	if (pin) {
		writeFileSync(
			join(former, "security.json"),
			JSON.stringify(FORMER_SECURITY),
		);
	}

	const app = await launchPane(context, {
		dataDir,
		derivedDataRoot: true,
		env: { HOME: home, CFFIXED_USER_HOME: home },
	});

	const launched = await app.state();

	if (pin) {
		expect(
			launched.security.locked && launched.security.pinLength === 4,
			`not locked behind the moved PIN: ${JSON.stringify(launched.security)}`,
		);

		await app.send("pinKeys", { keys: "1234" });
	}

	const state = await app.waitFor(
		"the moved data open",
		(current) =>
			!current.security.locked && current.security.data.phase === "open",
		60_000,
	);

	await app.send("save");

	const moved = {
		pin,
		profiles: state.profiles.map((profile) => profile.name),
		formerData: existsSync(former),
		formerCaches: existsSync(join(caches, "com.pane.desktop")),
		caches: existsSync(join(caches, "xyz.roundedcorners.pane")),
		sealed:
			typeof (
				JSON.parse(readFileSync(join(dataDir, "profiles.json"), "utf8")) as {
					box?: string;
				}
			).box === "string",
		userUntouched: JSON.stringify(userFolders()) === JSON.stringify(userBefore),
	};

	await app.browser.close();

	expect(
		moved.userUntouched,
		`the scenario reached the user's own folders: ${JSON.stringify(userBefore)}`,
	);

	expect(
		JSON.stringify(moved.profiles) === '["Moved"]' &&
			!moved.formerData &&
			!moved.formerCaches &&
			moved.caches &&
			moved.sealed,
		`the earlier identifier's data: ${JSON.stringify(moved)}`,
	);

	return moved;
}

const identifierMove: Scenario = async (context) => ({
	withoutPin: await move(context, false),
	withPin: await move(context, true),
});

export default identifierMove;
