import { tabOf } from "../lib/pane";
import { answerSheet, WindowWatcher, waitForSheet } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

const TARGET = "http://pane-proxy.test/";

// A proxy asking for credentials (407) gets the same sheet, saying it is the
// proxy that asks.
const proxyAuth: Scenario = async (context) => {
	const expected = `Basic ${btoa("proxy-user:proxy-secret")}`;
	const proxied: string[] = [];

	// A forward proxy that serves every request itself once authorized.
	const proxy = Bun.serve({
		hostname: "127.0.0.1",
		port: 0,
		fetch(request) {
			if (request.headers.get("Proxy-Authorization") !== expected) {
				return new Response("Proxy sign in required", {
					status: 407,
					headers: { "Proxy-Authenticate": 'Basic realm="Pane proxy"' },
				});
			}

			proxied.push(request.url);

			return new Response(
				"<!doctype html><title>Through the proxy</title><h1>Proxied</h1>",
				{ headers: { "Content-Type": "text/html" } },
			);
		},
	});

	try {
		const { app, has } = await launchTraced(context, "proxy-auth", {
			args: [`--proxy-server=http://127.0.0.1:${proxy.port}`],
		});

		const watcher = new WindowWatcher(app).start();
		const profile = await app.createProfile("Proxy");
		const tab = await app.openTab(profile, TARGET);

		const sheet = await waitForSheet(app, "authentication");
		expect(sheet.message?.startsWith("The proxy"), `sheet ${sheet.message}`);

		const answer = await answerSheet(app, "Sign In", [
			"proxy-user",
			"proxy-secret",
		]);

		const loaded = await app.waitFor(
			"the proxied page",
			(state) => tabOf(state, tab)?.title === "Through the proxy",
		);

		expect(
			proxied.some((url) => url.startsWith(TARGET)),
			`the proxy saw ${proxied.join(", ")}`,
		);

		expect(
			has(
				"PaneWebContentsDelegate.authenticate",
				(args) => args.proxy === true,
			),
			"trace lacks a proxy authenticate",
		);

		const windows = await watcher.stop();

		expect(
			windows.others.length === 0,
			`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
		);

		return { sheet, answer, tab: tabOf(loaded, tab), proxied, windows };
	} finally {
		proxy.stop(true);
	}
};

export default proxyAuth;
