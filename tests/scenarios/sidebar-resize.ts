import { join } from "node:path";
import { isScreenLocked, mainWindowOf, windowState } from "../lib/cua";
import { launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

type Drag = { stepMs: number[]; pageWidth: number };

// The sidebar's edge resizes it (Paper: 03.11 Sidebar · Scroll & resize): a
// drag follows the pointer between 200 and 400 points, a double-click returns
// to 260, and the width is kept across launches. Each step of a drag is timed
// from its event to the window's layout at the new width.
const sidebarResize: Scenario = async (context) => {
	const dataDir = context.tempDir("pane-sidebar-");
	const first = await launchPane(context, { dataDir });
	const profile = await first.createProfile("Resize");
	const url = context.fixtures.url("light.html?resize");
	const tab = await first.openTab(profile, url);
	await first.page(tab, url);

	// The list fades at its edges as it scrolls (the Electron app's
	// scroll-fade-y): enough tabs to scroll, drawn at the top and scrolled.
	for (let index = 0; index < 24; index++) {
		await first.send("openTab", {
			profile,
			url: context.fixtures.url(`light.html?tab=${index}`),
		});
	}

	await first.send("select", { tab });
	await Bun.sleep(500);

	// The window as the screen shows it: the fade is a layer mask, which the
	// app's own render leaves out. Locked, there is no screen to show.
	const shot = async (file: string) => {
		if (await isScreenLocked()) {
			context.defer(`${file}: the list's fade, which only the screen shows`);
		} else {
			const pid = first.browser.pid;
			await windowState(pid, await mainWindowOf(pid), join(context.out, file));
		}
	};

	await shot("list-top.png");
	await first.send("scrollWindow", { x: 130, y: 400, dy: -120 });
	await Bun.sleep(300);
	await shot("list-scrolled.png");

	const initial = (await first.state()).sidebarWidth;
	expect(initial === 260, `the sidebar starts at ${initial}`);

	// Halfway down the window, 2 points inside the edge.
	const y = 400;

	const drag = async (from: number, to: number) =>
		first.send<Drag>("dragWindow", { from: [from, y], to: [to, y], steps: 30 });

	const wider = await drag(258, 318);

	expect(
		wider.state.sidebarWidth === 320,
		`dragged 60 wider: ${wider.state.sidebarWidth}`,
	);

	// The panel follows: the window less the sidebar and the 8-point gutter.
	const window = Number(
		wider.state.window.frame.match(/\{([\d.]+), [\d.]+\}\}$/)?.[1],
	);

	expect(
		wider.result.pageWidth === window - 320 - 8,
		`the page is ${wider.result.pageWidth} wide in a ${window}-point window`,
	);

	const widest = await drag(318, 700);

	expect(
		widest.state.sidebarWidth === 400,
		`past the widest: ${widest.state.sidebarWidth}`,
	);

	const narrowest = await drag(398, 40);

	expect(
		narrowest.state.sidebarWidth === 200,
		`past the narrowest: ${narrowest.state.sidebarWidth}`,
	);

	// A double-click on the edge: the standard width.
	await first.send("clickWindow", { x: 198, y, clicks: 2 });

	const reset = await first.waitFor(
		"260 again",
		(state) => state.sidebarWidth === 260,
	);

	// Kept across launches.
	const kept = await drag(258, 298);

	expect(
		kept.state.sidebarWidth === 300,
		`dragged 40 wider: ${kept.state.sidebarWidth}`,
	);

	await first.send("save");
	await first.browser.close();

	const second = await launchPane(context, { dataDir });
	const relaunched = (await second.state()).sidebarWidth;
	expect(relaunched === 300, `after relaunching: ${relaunched}`);

	const steps = [wider, widest, narrowest, kept]
		.flatMap((each) => each.result.stepMs)
		.sort((a, b) => a - b);

	return {
		widths: {
			initial,
			wider: wider.state.sidebarWidth,
			widest: widest.state.sidebarWidth,
			narrowest: narrowest.state.sidebarWidth,
			reset: reset.sidebarWidth,
			relaunched,
		},
		pageWidth: wider.result.pageWidth,
		stepMs: {
			count: steps.length,
			median: steps[Math.floor(steps.length / 2)],
			p95: steps[Math.floor(steps.length * 0.95)],
			max: steps.at(-1),
		},
	};
};

export default sidebarResize;
