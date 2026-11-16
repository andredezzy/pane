// What Chrome asks for, shown by the app (ADR 0021). A page's dialog is
// answered by a real click on its button, through the automation channel,
// in the background too, its fields filled first as a value set through
// accessibility is; the system's panels by their own buttons.

import { expect } from "../run";
import type { AppState, PaneApp } from "./pane";
import { bySystem } from "./platform";
import { poll } from "./poll";
import { uiaPress, uiaTree } from "./uia";

export async function waitForSheet(
	app: PaneApp,
	kind: string,
	timeoutMs = 20_000,
): Promise<NonNullable<AppState["sheet"]>> {
	const state = await app.waitFor(
		`a ${kind} sheet`,
		(current) => current.sheet?.kind === kind,
		timeoutMs,
	);

	return state.sheet as NonNullable<AppState["sheet"]>;
}

// A dialog's button, as clicked: the channel presses it, so no input goes to
// the screen.
export async function answerSheet(
	app: PaneApp,
	button: string,
	texts: string[] = [],
): Promise<void> {
	await app.send("clickSheetButton", { title: button, texts });
}

// The system's print panel, app-modal, by its own button: the Mac's by
// performClick, through the channel; Windows 11's, the shell's, whose buttons
// are XAML's, which no window message presses, through UI Automation, as
// Narrator's user presses them (ADR 0071).
export const answerModal = bySystem({
	mac: async (app: PaneApp, button: string): Promise<void> => {
		await app.send("clickModalButton", { title: button });
	},
	windows: async (app: PaneApp, button: string): Promise<void> => {
		const { modal } = await app.state();

		if (!modal?.number) {
			// An older dialog of Windows', in Pane's own process.
			await app.send("clickModalButton", { title: button });

			return;
		}

		const pressed = await uiaPress(modal.number, button);

		expect(pressed === "invoked", `the print dialog's ${button}: ${pressed}`);
	},
});

// The print panel's buttons, by their titles: the Mac's as the channel lists
// them, Windows 11's as UI Automation reads them.
export const modalButtons = bySystem({
	mac: async (app: PaneApp): Promise<string[]> =>
		(await app.state()).modal?.buttons ?? [],
	windows: async (app: PaneApp): Promise<string[]> => {
		const { modal } = await app.state();

		if (!modal?.number) {
			return modal?.buttons ?? [];
		}

		return (await uiaTree(modal.number))
			.filter((element) => element.role === "Button")
			.map((element) => element.title);
	},
});

type NativeWindow = AppState["nativeWindows"][number];

// A menu's window: the system's on the Mac (NSPopupMenuWindow), Views' own on
// Windows (its MenuHost widget).
const MENU_WINDOW = bySystem({ mac: "NSPopupMenuWindow", windows: "MenuHost" });

// Samples the app's windows through a scenario: every visible window must be
// the app's own (its window, a sheet on it, an app-modal panel) or a menu
// (MENU_WINDOW: the page's menu, Chrome's own as U2 keeps it, and Pane's
// through MenuRunner and AnchoredMenu, which show with Pane in front).
// Anything else is Chrome's UI showing through, which stop() fails on, but for
// what `expected` allows.
export class WindowWatcher {
	private readonly others: NativeWindow[] = [];
	private timer: ReturnType<typeof setInterval> | null = null;

	constructor(
		private readonly app: PaneApp,
		private readonly expected: (window: NativeWindow) => boolean = () => false,
	) {}

	start(): this {
		this.timer = setInterval(() => {
			this.sample().catch(() => {});
		}, 400);

		return this;
	}

	async sample(): Promise<void> {
		const { nativeWindows } = await this.app.state();

		this.others.push(
			...nativeWindows.filter(
				(window) =>
					window.role === "other" &&
					window.class !== MENU_WINDOW &&
					!this.expected(window),
			),
		);
	}

	async stop(): Promise<void> {
		if (this.timer) {
			clearInterval(this.timer);
		}

		await this.sample();

		expect(
			this.others.length === 0,
			`Chrome windows appeared: ${JSON.stringify(this.others)}`,
		);
	}
}

// The page answer of a fixture's window.result, once set.
export async function pageResult<T>(
	evaluate: (expression: string) => Promise<unknown>,
	timeoutMs = 10_000,
): Promise<T> {
	return poll(
		"the page to report a result",
		() => evaluate("window.result").catch(() => undefined) as Promise<T>,
		timeoutMs,
	);
}
