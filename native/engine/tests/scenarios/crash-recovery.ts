import { tabOf } from "../lib/pane";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// The selected tab's renderer dies; the app hears of it and reloads the page,
// which comes back working.
const crashRecovery: Scenario = async (context) => {
	const { app, trace, waitForEntry } = await launchTraced(
		context,
		"crash-recovery",
	);

	const profile = await app.createProfile("Crash");
	const url = context.fixtures.url("crash.html");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);

	await page.evaluate("window.__beforeCrash = true");
	// The renderer dies before it can answer, so the reply never comes.
	page.send("Page.crash").catch(() => {});

	const terminated = await waitForEntry(
		"PaneWebContentsDelegate.rendererDidTerminate",
	).catch(() => null);

	expect(terminated, "the app was never told the renderer died");

	await app.waitFor(
		"the reload",
		(state) =>
			tabOf(state, tab)?.loading === false && tabOf(state, tab)?.url === url,
	);

	const deadline = Date.now() + 30_000;
	let recovered: { title: string; fresh: boolean } | null = null;

	while (!recovered && Date.now() < deadline) {
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

	const termination = trace().filter((entry) =>
		entry.name.endsWith("rendererDidTerminate"),
	);

	return { termination, recovered, tab: tabOf(await app.state(), tab) };
};

export default crashRecovery;
