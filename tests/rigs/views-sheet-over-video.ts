import { launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

// G4 on the Views prototype (docs/plans/views-migration.md, Phase 1): a
// sheet and its scrim over a live page playing video, in one window. Where
// the page lies under the sheet, a press goes to Views, not the page (asked
// through `hitTest`, which reports who the platform hands a press, so no
// input is needed in the background); the frames of the sheet's animation
// are counted from Chromium's PipelineReporter while the video plays. With
// the front (run.ts --foreground), real clicks and keys go to the sheet too.

type Reporter = { state: string; frame_source: number };

const viewsSheetOverVideo: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Video");

	const { page } = await app.openPage(
		profile,
		context.fixtures.url("pip.html?sheet"),
	);

	const playing = await page.evaluate<boolean>(
		"!document.getElementById('video').paused",
	);

	expect(playing, "the video is not playing");

	// The page's middle, then a point over the page left of the sheet's card,
	// and one on the card (the window is 1280 by 800, the card 400 wide at its
	// right).
	const middle = { x: 760, y: 420 };
	const scrim = { x: 420, y: 420 };
	const card = { x: 1060, y: 420 };

	const hit = async (point: { x: number; y: number }) =>
		(await app.send<{ handler: string; target: string }>("hitTest", point))
			.result;

	const bare = await hit(middle);
	expect(bare.target === "page", `the page's own press went to ${bare.target}`);

	const reporters: Reporter[] = [];
	const cdp = app.browser.cdp;

	const off = cdp.on(({ method, params, sessionId }) => {
		if (method !== "Tracing.dataCollected" || sessionId) {
			return;
		}

		for (const event of params.value as {
			name: string;
			args?: { frame_reporter?: Reporter };
		}[]) {
			if (event.name === "PipelineReporter" && event.args?.frame_reporter) {
				reporters.push(event.args.frame_reporter);
			}
		}
	});

	await cdp.send("Tracing.start", {
		transferMode: "ReportEvents",
		traceConfig: { includedCategories: ["benchmark"] },
	});

	await app.send("openProfileSheet");
	await Bun.sleep(400);

	const overScrim = await hit(scrim);
	const overCard = await hit(card);

	expect(
		overScrim.target === "views" && overCard.target === "views",
		`a press on the sheet reached the page: ${JSON.stringify({ overScrim, overCard })}`,
	);

	await app.send("closeProfileSheet");
	await Bun.sleep(400);

	const complete = cdp.waitFor("Tracing.tracingComplete", undefined, 30_000);
	await cdp.send("Tracing.end");
	await complete;
	off();

	const bySource = new Map<number, Record<string, number>>();

	for (const { frame_source, state } of reporters) {
		const counts = bySource.get(frame_source) ?? {};
		counts[state] = (counts[state] ?? 0) + 1;
		bySource.set(frame_source, counts);
	}

	if (context.foreground) {
		await app.send("openProfileSheet");
		const clicks = () => page.evaluate<number>("window.clicks ?? 0");

		await page.evaluate(
			"window.clicks = 0; addEventListener('click', () => window.clicks++)",
		);

		await app.send("clickWindow", scrim);
		await app.send("key", { key: "a" });
		const state = await app.state();
		expect((await clicks()) === 0, "a click on the scrim reached the page");
		expect(state.focus === "sheet", "the sheet lost the keyboard");
		await app.send("closeProfileSheet");
	} else {
		context.defer(
			"real clicks and keys into the sheet: they need Pane in front",
		);
	}

	await app.quit();

	return {
		handlers: { bare, overScrim, overCard },
		framesBySource: Object.fromEntries(bySource),
	};
};

export default viewsSheetOverVideo;
