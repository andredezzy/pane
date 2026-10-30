// Helpers for the Phase 3 scenarios: Chrome's dialogs and menus, shown by the
// app. A sheet is answered by clicking its real button: with cua-driver on an
// unlocked screen, else through the automation channel, which presses the
// same NSButton (performClick). Each answer records which way it went.
import { canDriveUI, clickButton, fillFields, mainWindowOf } from "./cua";
import type { AppState, PaneApp } from "./pane";
import { windowsOf } from "./windows";

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

type WindowSample = {
	at: number;
	label?: string;
	cgWindows: number;
	nativeWindows: AppState["nativeWindows"];
};

// Samples the app's windows through a scenario: every visible window must be
// the app's own (its window, a sheet on it, an app-modal panel). Anything
// else is Chrome's UI showing through.
export class WindowWatcher {
	readonly samples: WindowSample[] = [];
	private timer: ReturnType<typeof setInterval> | null = null;
	private readonly startedAt = Date.now();

	constructor(private readonly app: PaneApp) {}

	start(): this {
		this.timer = setInterval(() => {
			this.sample().catch(() => {});
		}, 400);

		return this;
	}

	async sample(label?: string): Promise<void> {
		const [state, cg] = await Promise.all([
			this.app.state(),
			windowsOf(this.app.browser.pid),
		]);

		this.samples.push({
			at: Date.now() - this.startedAt,
			label,
			cgWindows: cg.count,
			nativeWindows: state.nativeWindows,
		});
	}

	async stop(): Promise<WindowReport> {
		if (this.timer) {
			clearInterval(this.timer);
		}

		await this.sample("end");

		const others = this.samples.flatMap((sample) =>
			sample.nativeWindows
				.filter((window) => window.role === "other")
				.map((window) => ({ at: sample.at, ...window })),
		);

		return {
			samples: this.samples.length,
			maxCgWindows: Math.max(...this.samples.map((sample) => sample.cgWindows)),
			roles: [
				...new Set(
					this.samples.flatMap((sample) =>
						sample.nativeWindows.map((window) => window.role),
					),
				),
			],
			others,
			labeled: this.samples.filter((sample) => sample.label),
		};
	}
}

export type WindowReport = {
	samples: number;
	maxCgWindows: number;
	roles: string[];
	others: {
		at: number;
		role: string;
		class: string;
		title: string;
		frame: string;
	}[];
	labeled: WindowSample[];
};

// The page answer of a fixture's window.result, once set.
export async function pageResult<T>(
	evaluate: (expression: string) => Promise<unknown>,
	timeoutMs = 10_000,
): Promise<T> {
	const deadline = Date.now() + timeoutMs;

	while (Date.now() < deadline) {
		const value = await evaluate("window.result").catch(() => undefined);

		if (value !== undefined && value !== null) {
			return value as T;
		}

		await Bun.sleep(100);
	}

	throw new Error("the page never reported a result");
}
