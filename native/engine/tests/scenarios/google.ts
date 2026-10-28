import { type PaneApp, tabOf } from "../lib/pane";
import { waitForPage } from "../lib/targets";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// Google accepts the engine: its sign-in page shows the identifier field, in
// a profile without a fingerprint and in one that claims Windows, and never
// redirects to /rejected ("this browser or app may not be secure"). Nothing
// is typed and nobody signs in.

const WINDOWS = {
	userAgent:
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36",
	platform: "WINDOWS",
	screen: { width: 1920, height: 1080, colorDepth: 24 },
	language: "en-US",
	languages: ["en-US", "en"],
	timezone: "America/New_York",
	webgl: {
		vendor: "Google Inc. (NVIDIA)",
		renderer:
			"ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0, D3D11)",
	},
	hardwareConcurrency: 8,
	deviceMemory: 8,
	maxTouchPoints: 0,
};

// Google's sign-in page, as the profile gets it.
async function identifierPage(app: PaneApp, profile: string) {
	const tab = await app.openTab(profile, "https://accounts.google.com/");

	await app.waitFor(
		"accounts.google.com loaded",
		(state) => {
			const current = tabOf(state, tab);

			return (
				!!current &&
				!current.loading &&
				current.url.startsWith("https://accounts.google.com/") &&
				current.url !== "https://accounts.google.com/"
			);
		},
		45_000,
	);

	const page = await waitForPage(
		app.browser.cdp,
		"Google's sign-in page",
		(info) => info.url.startsWith("https://accounts.google.com/"),
	);

	const deadline = Date.now() + 20_000;
	let report = { url: "", identifier: false, title: "" };

	while (Date.now() < deadline) {
		report = await page.evaluate<typeof report>(
			`({ url: location.href, identifier: !!document.querySelector('input[type="email"], #identifierId'), title: document.title })`,
		);

		if (report.identifier) {
			break;
		}

		await Bun.sleep(500);
	}

	return {
		url: new URL(report.url),
		identifier: report.identifier,
		title: report.title,
	};
}

const google: Scenario = async (context) => {
	const { app } = await launchTraced(context, "google");
	const plain = await app.createProfile("Google", "BLUE");
	const windows = await app.createProfile("Google Windows", "EMERALD");

	await app.send("setProfileFingerprint", {
		profile: windows,
		fingerprint: WINDOWS,
	});

	const accepted = {
		plain: await identifierPage(app, plain),
		windows: await identifierPage(app, windows),
	};

	for (const [name, result] of Object.entries(accepted)) {
		expect(
			result.url.hostname === "accounts.google.com" &&
				!result.url.pathname.includes("/rejected") &&
				result.identifier,
			`${name}: Google showed ${result.url.href} (identifier field: ${result.identifier})`,
		);
	}

	return {
		accepted: Object.fromEntries(
			Object.entries(accepted).map(([name, result]) => [
				name,
				{
					url: `${result.url.origin}${result.url.pathname}`,
					identifierField: result.identifier,
					title: result.title,
				},
			]),
		),
	};
};

export default google;
