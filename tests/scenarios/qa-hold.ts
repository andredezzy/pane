import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { launchPane } from "../lib/pane";
import { sessionFolders } from "../lib/rich-session";
import type { Scenario } from "../run";

// A test copy for a pass by hand, by computer use (docs/plans/engine-file-encryption.md,
// Testing as a person would): Pane on throwaway data, with a PIN of 1234 and a
// profile, held open until `<out>/qa-stop` exists or PANE_QA_MINUTES have passed.
// `<out>/qa-session.json` says where it is. `--foreground` is what lets it take
// the front, for the steps that need it.

// bcryptjs 3.0.3, hashSync("1234", 10), as benchmark-unlock's.
const PIN_1234 = {
	state: {
		pin: {
			hash: "$2b$10$6RNvGASNquZ6f5LXjFap1eMcHOZTOzRYJVRe1DrjRG2juL90W5MNO",
			length: 4,
		},
		failedAttempts: 0,
	},
	version: 0,
};

const qaHold: Scenario = async (context) => {
	const folders = sessionFolders(context);

	if (process.env.PANE_QA_NO_PIN !== "1") {
		mkdirSync(folders.dataDir, { recursive: true });

		writeFileSync(
			join(folders.dataDir, "security.json"),
			JSON.stringify(PIN_1234),
		);
	}

	const app = await launchPane(context, {
		...folders.launch,
		env: {
			...folders.launch.env,
			...JSON.parse(process.env.PANE_QA_ENV ?? "{}"),
		},
	});

	const stop = join(context.out, "qa-stop");

	writeFileSync(
		join(context.out, "qa-session.json"),
		JSON.stringify(
			{
				pid: app.browser.pid,
				port: app.browser.port,
				dataDir: folders.dataDir,
				home: folders.home,
				automation: app.automationDir,
				fixtures: context.fixtures.origin,
			},
			null,
			"\t",
		),
	);

	const deadline =
		Date.now() + Number(process.env.PANE_QA_MINUTES ?? 120) * 60_000;

	while (!existsSync(stop) && Date.now() < deadline) {
		await Bun.sleep(500);
	}

	await app.quit();

	return { held: true };
};

export default qaHold;
