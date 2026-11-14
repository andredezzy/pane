import {
	type AppState,
	HAS_MENU_BAR,
	launchPane,
	PLACES_WINDOW_CONTROLS,
	trafficLightsInPlace,
} from "../lib/pane";
import { KEYS } from "../lib/platform";
import { poll } from "../lib/poll";
import { WindowWatcher } from "../lib/surfaces";
import { expect, type Scenario } from "../run";

// A page's fullscreen fills the app's window (no sidebar or toolbar), the
// app shows the Esc hint, and Esc leaves it. The window's full screen follows
// the page's, as Chrome's does: it enters with the page and leaves with it,
// even when Esc lands as the window is still entering; the user's own full
// screen stays when the page leaves; and leaving full screen takes the page
// out of its fullscreen. Each time the traffic lights end in place.
const fullscreen: Scenario = async (context) => {
	// Every check here needs Pane in front, which the app in use keeps
	// unless the runner allows it.
	if (!context.foreground) {
		context.defer("every check: it needs Pane in front (run.ts --foreground)");

		return;
	}

	const app = await launchPane(context);
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Fullscreen");
	const url = context.fixtures.url("fullscreen.html");
	const { page } = await app.openPage(profile, url);

	// Since Chromium 157 a page goes fullscreen only from a focused view, in
	// the key window: the user clicks the page first.
	const requestFullscreen = async () => {
		await app.send("activate");
		await app.waitFor("the key window", (state) => state.window.key, 10_000);
		await app.send("clickPage", { x: 20, y: 20 });

		await app.waitFor(
			"the page focused",
			(state) => state.focus === "pane.content",
		);

		await page.gesture("document.documentElement.requestFullscreen()", true);

		return app.waitFor("page fullscreen", (state) => state.pageFullscreen);
	};

	// A key posted during the move to full screen's own Space can be lost, so
	// Esc is pressed again until it lands.
	const pressEscape = async () => {
		let escapes = 0;

		const left = await poll(
			"fullscreen to end",
			async () => {
				await app.send("key", { key: "\u001b" });
				escapes++;

				return app
					.waitFor("fullscreen to end", (state) => !state.pageFullscreen, 1_000)
					.catch(() => null);
			},
			20_000,
			0,
		);

		return { left, escapes };
	};

	// The window in or out of full screen, its move over: the style mask
	// changes as the move starts, and AppKit's transition window goes as it
	// ends.
	const moved = (state: AppState) =>
		!state.nativeWindows.some((window) =>
			window.class.startsWith("_NSFullScreenTransition"),
		);

	const settled = async (fullScreen: boolean, description: string) => {
		await app.waitFor(
			description,
			(state) => state.window.fullScreen === fullScreen && moved(state),
		);

		// The windows of the Space's own move.
		await Bun.sleep(1_000);

		return app.state();
	};

	// View › Enter or Exit Full Screen, as the user's click: the item's
	// title follows the window's state when the menu opens. Where there is no
	// menu bar (Windows' home for its commands is W5's), its key, F11 there.
	// False when the menu bar has neither item.
	const toggleFromMenu = async () => {
		if (!HAS_MENU_BAR) {
			await app.send("key", KEYS.fullScreen);

			return true;
		}

		let missing = 0;

		for (const title of ["Exit Full Screen", "Enter Full Screen"]) {
			const ran = await app
				.send<boolean>("menu", { path: ["View", title] })
				.catch((error: unknown) => {
					if (String(error).includes("no such menu item")) {
						missing += 1;
					}

					return null;
				});

			if (ran?.result) {
				return true;
			}
		}

		if (missing === 2) {
			return false;
		}

		throw new Error("View › Enter or Exit Full Screen did not run");
	};

	if (!PLACES_WINDOW_CONTROLS) {
		context.defer(
			"the window's controls through full screen: Windows' caption buttons are the system frame's until Paper draws Pane's (W5)",
		);
	}

	const lightsInPlace = (state: AppState, when: string) => {
		const lights = state.window.trafficLightFrames;

		if (!PLACES_WINDOW_CONTROLS) {
			return lights;
		}

		expect(
			trafficLightsInPlace(lights),
			`the traffic lights ${when}: ${JSON.stringify(lights)}`,
		);

		return lights;
	};

	// The page's fullscreen, and Esc pressed as soon as the window starts
	// its move: the window still leaves full screen once it has entered.
	const entered = await requestFullscreen();

	const element = await page.evaluate<string | null>(
		"document.fullscreenElement?.tagName ?? null",
	);

	await watcher.sample();

	await app.waitFor(
		"the window in full screen",
		(state) => state.window.fullScreen,
	);

	await pressEscape();

	const after = await page.evaluate<string | null>(
		"document.fullscreenElement?.tagName ?? null",
	);

	// AppKit makes the window's frame view anew as its style mask changes,
	// and the new one put the traffic lights at its own (9, 9).
	const windowed = await settled(false, "the window out of full screen");

	lightsInPlace(windowed, "after the page's fullscreen");

	expect(element === "HTML", `fullscreen element ${element}`);

	// The Views build draws no hint until What Chrome asks for's full screen
	// (ADR 0025), and its state has no such key until then.
	if ("exclusiveAccessHint" in entered) {
		expect(
			entered.exclusiveAccessHint?.includes("Esc"),
			`hint ${entered.exclusiveAccessHint}`,
		);
	} else {
		context.defer(
			"the Esc hint: no hint is drawn (What Chrome asks for's full screen)",
		);
	}

	expect(after === null, "Esc did not leave fullscreen");

	// The user's own full screen, then the page's fullscreen in it: leaving
	// the page's keeps the window in full screen.
	if (!(await toggleFromMenu())) {
		context.defer(
			"the user's full screen from View › Enter Full Screen: the menu bar is H7's",
		);

		await watcher.stop();

		return;
	}

	await settled(true, "the user's full screen");
	await requestFullscreen();
	await pressEscape();
	const kept = await settled(true, "the user's full screen, kept");

	expect(
		kept.window.fullScreen && !kept.pageFullscreen,
		`leaving the page's fullscreen left the user's full screen: ${JSON.stringify(kept.window)}`,
	);

	// The page's fullscreen again, and the user leaves full screen: the page
	// leaves its fullscreen with it.
	await requestFullscreen();
	await toggleFromMenu();
	const userLeft = await settled(false, "the window out of full screen");

	await app.waitFor(
		"the page out of fullscreen",
		(state) => !state.pageFullscreen,
	);

	lightsInPlace(userLeft, "after the user's full screen");

	await watcher.stop();
};

export default fullscreen;
