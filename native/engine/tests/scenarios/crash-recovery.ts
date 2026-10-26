import { readFileSync, rmSync } from "node:fs";
import { join } from "node:path";
import { launchPane, tabOf } from "../lib/pane";
import { expect, type Scenario } from "../run";

// The selected tab's renderer dies; the app hears of it and reloads the page,
// which comes back working.
const crashRecovery: Scenario = async (context) => {
	const trace = join(context.out, "crash-recovery-trace.jsonl");
	rmSync(trace, { force: true });

	const app = await launchPane(context, { env: { PANE_ENGINE_TRACE: trace } });
	const profile = await app.createProfile("Crash");
	const url = context.fixtures.url("crash.html");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);

	await page.evaluate("window.__beforeCrash = true");
	// The renderer dies before it can answer, so the reply never comes.
	page.send("Page.crash").catch(() => {});

	const deadline = Date.now() + 20_000;
	let terminated = false;

	while (!terminated && Date.now() < deadline) {
		terminated = readFileSync(trace, "utf8").includes("rendererDidTerminate");
		await Bun.sleep(200);
	}

	expect(terminated, "the app was never told the renderer died");

	await app.waitFor(
		"the reload",
		(state) =>
			tabOf(state, tab)?.loading === false && tabOf(state, tab)?.url === url,
	);

	let recovered: { title: string; fresh: boolean } | null = null;

	while (!recovered && Date.now() < deadline + 10_000) {
		try {
			const again = await app.page(tab, url);

			recovered = await again.evaluate(
				"({ title: document.title, fresh: window.__beforeCrash === undefined })",
			);
		} catch {
			await Bun.sleep(300);
		}
	}

	expect(recovered?.title === "Crash me", "the page did not come back");
	expect(recovered?.fresh, "the page was not reloaded");

	const termination = readFileSync(trace, "utf8")
		.split("\n")
		.filter((line) => line.includes("rendererDidTerminate"));

	return { termination, recovered, tab: tabOf(await app.state(), tab) };
};

export default crashRecovery;
