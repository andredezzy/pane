import { mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { $ } from "bun";
import { tabOf } from "../lib/pane";
import { WindowWatcher } from "../lib/surfaces";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

type Target = { targetId: string; type: string; url: string; title: string };

// A site with a certificate Chrome does not trust shows Chrome's warning page
// inside the tab (web content, not a window); proceeding loads the site.
const certificateInterstitial: Scenario = async (context) => {
	const folder = mkdtempSync(join(tmpdir(), "pane-tls-"));
	await $`openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=127.0.0.1 -keyout ${join(folder, "key.pem")} -out ${join(folder, "cert.pem")}`.quiet();

	const server = Bun.serve({
		hostname: "127.0.0.1",
		port: 0,
		tls: {
			cert: readFileSync(join(folder, "cert.pem"), "utf8"),
			key: readFileSync(join(folder, "key.pem"), "utf8"),
		},
		fetch: () =>
			new Response(
				"<!doctype html><title>Secure fixture</title><h1>Behind a self-signed certificate</h1>",
				{
					headers: { "Content-Type": "text/html" },
				},
			),
	});

	try {
		const { app, has } = await launchTraced(
			context,
			"certificate-interstitial",
		);

		const watcher = new WindowWatcher(app).start();
		const profile = await app.createProfile("TLS");
		const url = `https://127.0.0.1:${server.port}/`;
		const tab = await app.openTab(profile, url);

		const warned = await app.waitFor(
			"the certificate warning",
			(state) => tabOf(state, tab)?.title === "Privacy error",
			30_000,
		);

		await watcher.sample("interstitial");

		// The warning page's own "Proceed" link, through CDP.
		const { targetInfos } = await app.browser.cdp.send<{
			targetInfos: Target[];
		}>("Target.getTargets");

		const target = targetInfos.find(
			(info) => info.type === "page" && info.title === "Privacy error",
		);

		expect(target, "no target for the warning page");

		const { sessionId } = await app.browser.cdp.send<{ sessionId: string }>(
			"Target.attachToTarget",
			{
				targetId: target?.targetId,
				flatten: true,
			},
		);

		await app.browser.cdp.send(
			"Runtime.evaluate",
			{
				expression:
					"document.getElementById('details-button').click(); document.getElementById('proceed-link').click()",
			},
			sessionId,
		);

		const proceeded = await app.waitFor(
			"the site",
			(state) => tabOf(state, tab)?.title === "Secure fixture",
			30_000,
		);

		expect(
			has(
				"PaneWebContentsDelegate.didChangeTitle",
				(args) => args.title === "Privacy error",
			),
			"trace lacks the warning page",
		);

		const windows = await watcher.stop();

		expect(
			windows.others.length === 0,
			`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
		);

		return {
			warned: tabOf(warned, tab),
			proceeded: tabOf(proceeded, tab),
			windows,
		};
	} finally {
		server.stop(true);
	}
};

export default certificateInterstitial;
