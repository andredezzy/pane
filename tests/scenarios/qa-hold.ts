import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { launchPane } from "../lib/pane";
import {
	richSession,
	STORES_COMMITTED_MS,
	sessionFolders,
} from "../lib/rich-session";
import type { Scenario, ScenarioContext } from "../run";

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

// With PANE_QA_EARLIER naming an earlier Pane, a large profile it wrote, for the
// migration's pass: its tab is the session's, its data left closed.
async function earlierProfile(context: ScenarioContext, earlier: string) {
	const session = await richSession(context, earlier);
	const batches = Number(process.env.PANE_QA_BATCHES ?? 150);

	await session.page.evaluate(`window.fill = (first) => new Promise((resolve, reject) => {
		const opened = indexedDB.open("rich-session", 1);
		opened.onerror = () => reject(opened.error);
		opened.onsuccess = () => {
			const tx = opened.result.transaction("items", "readwrite");
			for (let i = 0; i < 10; i++) {
				const bytes = new Uint8Array(1024 * 1024);
				for (let at = 0; at < bytes.length; at += 65536) {
					crypto.getRandomValues(bytes.subarray(at, Math.min(at + 65536, bytes.length)));
				}
				tx.objectStore("items").put(new Blob([bytes]), "large-" + (first + i));
			}
			tx.oncomplete = () => { opened.result.close(); resolve(); };
			tx.onerror = () => reject(tx.error);
		};
	})`);

	for (let batch = 0; batch < batches; batch++) {
		await session.page.evaluate(`fill(${batch * 10})`);
	}

	await Bun.sleep(STORES_COMMITTED_MS);
	await session.app.browser.close();

	return session;
}

const qaHold: Scenario = async (context) => {
	const earlier = process.env.PANE_QA_EARLIER;
	const session = earlier ? await earlierProfile(context, earlier) : null;

	const folders = session
		? {
				dataDir: session.roots.dataRoot,
				home: session.roots.home,
				launch: { ...session.launch, binary: undefined },
			}
		: sessionFolders(context);

	if (!session && process.env.PANE_QA_NO_PIN !== "1") {
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
				...(session && {
					tab: session.tab,
					url: session.url,
					profile: session.profile,
					planted: session.planted,
				}),
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
