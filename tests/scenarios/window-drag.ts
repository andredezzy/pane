import { launchPane } from "../lib/pane";
import { poll } from "../lib/poll";
import { expect, type Scenario } from "../run";

// The window moves where the Electron app's did (its root and the lock
// screen's panel were drag regions): the sidebar's header, title included,
// the gutters around the content panel, and the lock screen's panel. Controls,
// the page, and the sidebar's list keep their clicks. Each point is
// hit-tested in the window, which is where a mouse down lands: a press the
// frame takes moves the window ("drag"), as AppKit's frame view drags it
// where Views answers HTCAPTION.
const DRAG = "drag";

const windowDrag: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Drag");
	const url = context.fixtures.url("smoke.html?drag");
	await app.openPage(profile, url);

	const frame = (await app.state()).window.frame;

	const [width, height] = (frame.match(/\{([\d.]+), ([\d.]+)\}\}$/) ?? [])
		.slice(1)
		.map(Number);

	expect(width > 0 && height > 0, `window frame: ${frame}`);

	const hit = async (x: number, y: number) => (await app.hitTest(x, y)).target;

	const points = {
		headerGap: [150, 20],
		headerTitle: [95, 20],
		topGutter: [700, 4],
		rightGutter: [width - 4, height / 2],
		bottomGutter: [700, height - 4],
		closeButton: [18, 18],
		page: [700, height / 2],
		sidebarList: [110, 120],
		settingsButton: [60, height - 22],
	} as const;

	const hits: Record<string, string> = {};

	for (const [name, [x, y]] of Object.entries(points)) {
		hits[name] = await hit(x, y);
	}

	for (const name of [
		"headerGap",
		"headerTitle",
		"topGutter",
		"rightGutter",
		"bottomGutter",
	]) {
		expect(
			hits[name] === DRAG,
			`${name} does not move the window: ${hits[name]}`,
		);
	}

	for (const name of ["closeButton", "page", "sidebarList", "settingsButton"]) {
		expect(hits[name] !== DRAG, `${name} moves the window: ${hits[name]}`);
	}

	// The lock screen's panel, around its numpad.
	await app.send("showPinScreen", { mode: "setup" });

	await app.waitFor(
		"the PIN screen",
		(state) => state.security.screen !== null,
	);

	// The screen slides in; the Views build draws it once L1 is in.
	const lockShown = await poll(
		"the lock screen",
		async () =>
			(await app.hitTest(width - 60, 60)).view.startsWith("pane.lockScreen"),
		2_000,
	).catch(() => false);

	if (!lockShown) {
		context.defer("the lock screen's panel: no lock screen is drawn (L1)");

		return;
	}

	// The screen slides in: its panel is there once the point hits it.
	const lockPanel = await poll(
		"the lock panel",
		async () => {
			const view = await hit(width - 60, 60);

			return view === DRAG && view;
		},
		5_000,
	).catch(() => hit(width - 60, 60));

	const lockNumpad = await hit(width / 2, height / 2);

	expect(
		lockPanel === DRAG,
		`the lock panel does not move the window: ${lockPanel}`,
	);

	expect(lockNumpad !== DRAG, `the numpad moves the window: ${lockNumpad}`);
};

export default windowDrag;
