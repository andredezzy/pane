// What Chrome asks for, shown by the app (ADR 0021). A page's dialog is
// answered by a real click on its button, through the automation channel,
// in the background too, its fields filled first as a value set through
// accessibility is; the system's panels by their own buttons.

import { expect } from "../run";
import type { AppState, PaneApp } from "./pane";
import { poll } from "./poll";

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

export async function answerSheet(
	app: PaneApp,
	button: string,
	texts: string[] = [],
): Promise<void> {
	await app.send("clickSheetButton", { title: button, texts });
}

// The system's print panel, app-modal, by its own button (performClick).
export async function answerModal(app: PaneApp, button: string): Promise<void> {
	await app.send("clickModalButton", { title: button });
}

type NativeWindow = AppState["nativeWindows"][number];

// Samples the app's windows through a scenario: every visible window must be
// the app's own (its window, a sheet on it, an app-modal panel) or a menu the
// system draws (NSPopupMenuWindow: the page's menu, Chrome's own as U2 keeps
// it, and Pane's through MenuRunner, which show with Pane in front). Anything
// else is Chrome's UI showing through, which stop() fails on, but for what
// `expected` allows.
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
					window.class !== "NSPopupMenuWindow" &&
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
