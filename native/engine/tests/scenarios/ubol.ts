import { installFromWebStore } from "../lib/extensions";
import { WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

const UBLOCK_LITE = "ddkjiahejlhfcafbddmgiahcphecmpfh";
const AD_SCRIPT = "https://securepubads.g.doubleclick.net/tag/js/gpt.js";
const AD_IMAGE = "https://ad.doubleclick.net/ddm/trackimp/pane-fixture";

type Failed = { requestId: string; errorText: string; blockedReason?: string };

// uBlock Origin Lite from the Chrome Web Store keeps a fixture's ad requests
// from their servers, as the CDP network log shows: it blocks the ad image,
// and swaps the ad script for its own inert stand-in (a redirect to the
// extension).
const ubol: Scenario = async (context) => {
	const { app } = await launchTraced(context, "ubol");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Blocking");

	const installed = await installFromWebStore(app, profile, UBLOCK_LITE);
	// Its rules load with the extension; give them a moment.
	await Bun.sleep(3_000);

	const blank = context.fixtures.url("isolation.html?ubol");
	const tab = await app.openTab(profile, blank);
	const page = await app.page(tab, blank);
	await page.send("Network.enable");

	// Each request's URLs, redirects included.
	const chains = new Map<string, string[]>();
	const failed: Failed[] = [];

	const off = app.browser.cdp.on((message) => {
		if (message.sessionId !== page.sessionId) {
			return;
		}

		if (message.method === "Network.requestWillBeSent") {
			const params = message.params as {
				requestId: string;
				request: { url: string };
			};

			chains.set(params.requestId, [
				...(chains.get(params.requestId) ?? []),
				params.request.url,
			]);
		}

		if (message.method === "Network.loadingFailed") {
			failed.push(message.params as Failed);
		}
	});

	await page.navigate(context.fixtures.url("ads.html"));
	await Bun.sleep(3_000);
	off();

	const outcome = (url: string) => {
		const [id, chain] =
			[...chains.entries()].find(([, urls]) => urls[0] === url) ?? [];

		if (!id || !chain) {
			return "never requested";
		}

		const failure = failed.find((entry) => entry.requestId === id);

		if (failure) {
			return failure.errorText;
		}

		const last = chain.at(-1) ?? "";

		return last.startsWith(`chrome-extension://${UBLOCK_LITE}/`)
			? `swapped for ${last}`
			: `loaded from ${last}`;
	};

	const script = outcome(AD_SCRIPT);
	const image = outcome(AD_IMAGE);

	expect(
		script === "net::ERR_BLOCKED_BY_CLIENT" || script.startsWith("swapped"),
		`the ad script: ${script}`,
	);

	expect(image === "net::ERR_BLOCKED_BY_CLIENT", `the ad image: ${image}`);

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return {
		installed,
		adScript: { url: AD_SCRIPT, outcome: script },
		adImage: { url: AD_IMAGE, outcome: image },
		requests: [...chains.values()],
		windows,
	};
};

export default ubol;
