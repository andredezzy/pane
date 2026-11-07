// Helpers for the Phase 3 scenarios: Chrome's dialogs and menus, shown by the
// app. A sheet is answered by clicking its real button: with cua-driver on an
// unlocked screen, else through the automation channel, which presses the
// same NSButton (performClick). Each answer records which way it went.

import { expect } from "../run";
import { canDriveUI, clickButton, fillFields, mainWindowOf } from "./cua";
import type { AppState, PaneApp } from "./pane";
import { poll } from "./poll";

export type Answer = { button: string; via: "cua" | "automation" };

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
): Promise<Answer> {
	if (await canDriveUI()) {
		const windowId = await mainWindowOf(app.browser.pid);

		if (texts.length > 0) {
			await fillFields(app.browser.pid, windowId, texts);
		}

		await clickButton(app.browser.pid, windowId, button);

		return { button, via: "cua" };
	}

	await app.send("clickSheetButton", { title: button, texts });

	return { button, via: "automation" };
}

export async function answerModal(
	app: PaneApp,
	button: string,
): Promise<Answer> {
	if (await canDriveUI()) {
		await clickButton(app.browser.pid, "any", button);

		return { button, via: "cua" };
	}

	await app.send("clickModalButton", { title: button });

	return { button, via: "automation" };
}

type NativeWindow = AppState["nativeWindows"][number];

// Samples the app's windows through a scenario: every visible window must be
// the app's own (its window, a sheet on it, an app-modal panel). Anything else
// is Chrome's UI showing through, which stop() fails on, but for what
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
				(window) => window.role === "other" && !this.expected(window),
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
