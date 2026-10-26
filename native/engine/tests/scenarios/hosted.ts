import { readFileSync, rmSync } from "node:fs";
import { join } from "node:path";
import { hotkey, isScreenLocked, mainWindowOf, windowState } from "../lib/cua";
import { windowsOf } from "../lib/windows";
import { expect, type Scenario } from "../run";

type Target = { type: string; url: string; title: string };

const URL = "https://example.com/";
const TITLE = "Example Domain";

async function waitForPage(port: number): Promise<Target> {
	const deadline = Date.now() + 30_000;

	while (Date.now() < deadline) {
		const targets = (await (
			await fetch(`http://127.0.0.1:${port}/json/list`)
		).json()) as Target[];

		const page = targets.find(
			(target) =>
				target.type === "page" && target.url === URL && target.title === TITLE,
		);

		if (page) {
			return page;
		}

		await Bun.sleep(250);
	}

	throw new Error(`no page target ${URL} titled ${TITLE}`);
}

async function helperCount(pid: number): Promise<number> {
	const output = await Bun.$`pgrep -P ${pid}`.quiet().nothrow().text();

	return output.split("\n").filter(Boolean).length;
}

// Phase 1: the Pane app hosts the engine. One window of the app's own, the
// page reported as a DevTools page, the app↔engine traffic in the trace, and
// — with an unlocked screen — the page visible in that window and Cmd+Q
// shutting every helper down.
const hosted: Scenario = async ({ launch, out }) => {
	const trace = join(out, "trace.jsonl");
	rmSync(trace, { force: true });

	const browser = await launch({
		args: [URL],
		env: { PANE_ENGINE_TRACE: trace },
	});

	const page = await waitForPage(browser.port);
	const windows = await windowsOf(browser.pid);

	expect(windows.count === 1, `expected 1 window, got ${windows.count}`);

	const calls = readFileSync(trace, "utf8")
		.split("\n")
		.filter(Boolean)
		.map(
			(line) =>
				JSON.parse(line) as {
					kind: string;
					name: string;
					args: Record<string, unknown>;
				},
		);

	const names = new Set(calls.map((entry) => entry.name));

	for (const name of ["PaneBrowserWindow.openTab"]) {
		expect(names.has(name), `trace lacks ${name}`);
	}

	expect(
		calls.some(
			(entry) =>
				entry.name === "PaneWebContentsDelegate.didChangeTitle" &&
				entry.args.title === TITLE,
		),
		`trace lacks didChangeTitle(${TITLE})`,
	);

	if (await isScreenLocked()) {
		return {
			page,
			windows,
			traceEntries: calls.length,
			visual: "deferred: screen locked",
		};
	}

	const windowId = await mainWindowOf(browser.pid);

	const state = await windowState(
		browser.pid,
		windowId,
		join(out, "window.png"),
	);

	const tree = state.tree_markdown;

	expect(!state.degraded, `window state degraded: ${state.degraded_reason}`);
	expect(/Sidebar/.test(tree), "accessibility tree lacks the sidebar");

	expect(
		/Web content/.test(tree),
		"accessibility tree lacks the web content area",
	);

	expect(
		/documentation examples/.test(tree),
		"accessibility tree lacks the page text",
	);

	const helpersBefore = await helperCount(browser.pid);
	await hotkey(browser.pid, windowId, ["cmd", "q"]);
	await Bun.sleep(5_000);

	const exited =
		(await Bun.$`kill -0 ${browser.pid}`.quiet().nothrow()).exitCode !== 0;

	const helpersAfter =
		await Bun.$`pgrep -f ${"Pane.app/Contents/Frameworks/PaneCore.framework"}`
			.quiet()
			.nothrow()
			.text();

	expect(exited, "the app still runs 5 s after Cmd+Q");

	expect(
		helpersAfter.trim() === "",
		`helpers still running after Cmd+Q:\n${helpersAfter}`,
	);

	return {
		page,
		windows,
		traceEntries: calls.length,
		visual: "verified",
		windowTitle: state.window_title,
		accessibility: tree,
		quit: { helpersBefore, exited, helpersAfter: 0 },
	};
};

export default hosted;
