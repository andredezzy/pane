import { expect, type ScenarioContext } from "../run";
import { canDriveUI, pressKey } from "./computer-use";
import type { PaneApp } from "./pane";
import { pollValue } from "./poll";
import { type MenuWindow, menusOf, windowsOf } from "./windows";

type Box = { x: number; y: number; width: number; height: number };

// Where a picker's menu showed, in points from the main screen's top left:
// its trigger, the click that opened it, and the menu.
export type MenuPlacement = {
	trigger: Box;
	click: { x: number; y: number };
	menu: Box;
};

const area = (bounds: Record<string, number>) =>
	(bounds.Width ?? 0) * (bounds.Height ?? 0);

// A picker's menu (a MenuButton; `name` its view's, "pane.profile.timeZone"),
// opened in front as a hand opens it: a click at the trigger's middle, its
// press and release posted through AppKit's queue as the trackpad's are, the
// press the event the system places a context menu by. The menu shows just
// under the trigger, its top left at the trigger's bottom left, not where the
// pointer pressed (Paper 05.7; Chromium's runner put it at the click), but
// where an edge of the screen pushed it in, as the system keeps a menu on its
// screen; the press's release leaves it open; Esc closes it, the choice as it
// was. With `choice`, it opens again and keys choose: Down that many times,
// then Return. The menu runs with the app's tasks held, so its window is read
// from the window list, and its keys go to the copy's process alone, through
// Aside Computer Use. In the background the system's menu would show over
// the user's apps: deferred.
export async function checkMenuUnderTrigger(
	app: PaneApp,
	context: ScenarioContext,
	name: string,
	choice?: { downs: number; chosen: string },
): Promise<MenuPlacement | undefined> {
	if (!context.foreground || !(await canDriveUI())) {
		context.defer(
			`${name}'s menu under its trigger, by a click and by keys: it needs Pane in front (run.ts --foreground) and Aside Computer Use`,
		);

		return undefined;
	}

	const pid = app.browser.pid;

	const shown = async () =>
		(
			await app.send<Record<string, { name: string; value: string }>>(
				"accessibility",
				{ prefix: name },
			)
		).result[name]?.value;

	const [x = 0, y = 0, width = 0, height = 0] =
		(await app.frames(name))[name] ?? [];

	expect(width > 0 && height > 0, `${name} doesn't show`);

	const [window] = (await windowsOf(pid)).windows.sort(
		(a, b) => area(b.bounds) - area(a.bounds),
	);

	expect(window, "the copy has no window on screen");

	const trigger = {
		x: (window.bounds.X ?? 0) + x,
		y: (window.bounds.Y ?? 0) + y,
		width,
		height,
	};

	const click = { x: trigger.x + width / 2, y: trigger.y + height / 2 };

	const open = async (): Promise<MenuWindow> => {
		await app.send("clickWindow", { x: x + width / 2, y: y + height / 2 });

		const menus = await pollValue(
			() => menusOf(pid),
			(open) => open.length === 1,
			5_000,
			50,
		);

		const [menu] = menus;

		expect(
			menu && menus.length === 1,
			`${name}'s menu didn't show alone: ${JSON.stringify(menus)}`,
		);

		return menu;
	};

	const closed = async (by: string) => {
		const menus = await pollValue(
			() => menusOf(pid),
			(open) => open.length === 0,
			5_000,
			50,
		);

		expect(menus.length === 0, `${by} left ${name}'s menu open`);
	};

	const key = (key: string) => pressKey(pid, key);

	const before = await shown();
	const { bounds, screen } = await open();

	// The click's release went to the menu, which stays.
	await Bun.sleep(400);

	expect(
		(await menusOf(pid)).length === 1,
		`${name}'s menu closed with the click's release`,
	);

	await key("escape");
	await closed("Esc");

	const after = await shown();

	expect(
		after === before,
		`${name} shows ${JSON.stringify(after)} after Esc, not ${JSON.stringify(before)}`,
	);

	const menu = {
		x: bounds.X,
		y: bounds.Y,
		width: bounds.Width,
		height: bounds.Height,
	};

	const placement = { trigger, click, menu };
	const anchor = { x: trigger.x, y: trigger.y + height };

	// An edge of the screen the menu would come near at its anchor has pushed
	// it in: AppKit keeps a menu a few points inside the screen's visible frame
	// (the time zone's, 272 points tall under a trigger whose bottom stood 715
	// down a 982-point screen, showed 11 higher, its bottom 6 short of the edge).
	const edge = screen.visible;
	const inset = 8;
	const pushedLeft = anchor.x + menu.width > edge.X + edge.Width - inset;
	const pushedUp = anchor.y + menu.height > edge.Y + edge.Height - inset;

	// At the anchor, or pushed in only as far as the edge asks: against it.
	const placed = (
		at: number,
		anchored: number,
		pushed: boolean,
		size: number,
		end: number,
	) =>
		Math.abs(at - anchored) <= 1 ||
		(pushed && at < anchored && at + size >= end - inset);

	expect(
		placed(menu.x, anchor.x, pushedLeft, menu.width, edge.X + edge.Width) &&
			placed(menu.y, anchor.y, pushedUp, menu.height, edge.Y + edge.Height),
		`${name}'s menu at ${menu.x}, ${menu.y}, not under its trigger at ${anchor.x}, ${anchor.y}: ${JSON.stringify(placement)}`,
	);

	// Its left edge tells the trigger's from the click's, half the trigger's
	// width apart, unless the screen's right edge moved it there.
	if (pushedLeft && Math.abs(menu.x - anchor.x) > 1) {
		context.defer(
			`${name}'s menu: the screen's right edge moved it, so its place doesn't show where it was placed`,
		);
	}

	if (choice) {
		await open();

		for (let down = 0; down < choice.downs; down++) {
			await key("down");
		}

		await key("return");
		await closed("Return");

		const chosen = await pollValue(
			shown,
			(value) => value === choice.chosen,
			5_000,
		);

		expect(
			chosen === choice.chosen,
			`${name} shows ${JSON.stringify(chosen)} after its keys, not ${JSON.stringify(choice.chosen)}`,
		);
	}

	return placement;
}
