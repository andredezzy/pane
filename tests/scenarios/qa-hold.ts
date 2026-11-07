import { existsSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { launchPane, setUpPin } from "../lib/pane";
import { sessionFolders } from "../lib/rich-session";
import type { Scenario } from "../run";

// A test copy for a pass by hand, by computer use (docs/plans/engine-file-encryption.md,
// Testing as a person would): Pane on throwaway data, with a PIN of 1234 and a
// profile, held open until `<out>/qa-stop` exists or PANE_QA_MINUTES have passed.
// `<out>/qa-session.json` says where it is. `--foreground` is what lets it take
// the front, for the steps that need it.

const qaHold: Scenario = async (context) => {
	const folders = sessionFolders(context);

	if (process.env.PANE_QA_NO_PIN !== "1") {
		const first = await launchPane(context, folders.launch);
		await setUpPin(first, "1234");
		await first.browser.close();
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
