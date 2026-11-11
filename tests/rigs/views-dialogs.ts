import { stats } from "../lib/benchmark";
import { launchPane, type PaneApp, pressWindowKey, tabOf } from "../lib/pane";
import { poll } from "../lib/poll";
import { signpostFrameIntervals } from "../lib/signposts";
import { pageResult, waitForSheet } from "../lib/surfaces";
import { clickAt } from "../lib/traced";
import { expect, type Scenario } from "../run";

// What Chrome asks for, first half (docs/plans/views-migration.md, Phase 3),
// on the Views build, in the background: every page's dialog drawn as
// `surfaces-dialog-<state>.png`, the names tests/rigs/surfaces.ts gave the
// Swift's renders, in the run's appearance (run.ts --appearance light|dark,
// one --out folder each), and states those renders miss (a field being
// edited, a message long enough to scroll); what VoiceOver reads of a dialog
// (ADR 0071); where Chrome's own tab-modal dialogs would show; and, timed by
// the app's signposts from Chrome's hook to the frame, a JavaScript alert's
// first frame ("Page dialog") and a permission prompt's ("Permission
// prompt"); and the switcher over a page's dialog, which takes Esc first, the
// dialog's Esc then (Views' own dispatch in the background, posted with
// --foreground). For the Views build, from out/Release:
//
//   bun tests/run.ts --binary ~/chromium/src/out/Release/Pane.app/Contents/MacOS/Pane \
//     --out <dir>/light --appearance light tests/rigs/views-dialogs.ts

const ROUNDS = 20;

// A dialog's button, as clicked: the channel's real click at its middle.
async function answer(app: PaneApp, button: string, texts: string[] = []) {
	await app.send("clickSheetButton", { title: button, texts });
	await app.waitFor("the dialog answered", (state) => state.sheet === null);
}

async function render(app: PaneApp, name: string) {
	// Past the entrance (0.15 s) and its frame.
	await app.render(`surfaces-${name}.png`, 400);
}

const viewsDialogs: Scenario = async (context) => {
	const { fixtures } = context;
	const app = await launchPane(context);
	const work = await app.createProfile("Work", "BLUE");
	const result: Record<string, unknown> = {};

	// A page's own dialogs: alert, confirm, prompt, its field being edited, a
	// message long enough to scroll, beforeunload on a reload and a close,
	// and a form's resubmission.
	const { tab, page } = await app.openPage(
		work,
		fixtures.url("dialogs.html?surfaces"),
	);

	for (const [run, name, button] of [
		["runAlert()", "dialog-alert", "OK"],
		["runConfirm()", "dialog-confirm", "Cancel"],
		["runPrompt()", "dialog-prompt", "Cancel"],
	] as const) {
		await page.evaluate(run);
		await waitForSheet(app, "javaScript");
		await render(app, name);

		if (name === "dialog-prompt") {
			result.promptAccessibility = (
				await app.send("dialogAccessibility")
			).result;

			// Its field edited, as a value set through accessibility edits it:
			// the edge turns blue.
			await app.send("fillSheet", { texts: ["Typed in Pane"] });
			await render(app, "dialog-prompt-editing");
		}

		await answer(app, button);
	}

	await page.evaluate(
		`later(() => alert(${JSON.stringify(
			Array.from(
				{ length: 60 },
				(_, line) => `Line ${line + 1} of a message long enough to scroll.`,
			).join("\n"),
		)}))`,
	);

	await waitForSheet(app, "javaScript");
	await render(app, "dialog-alert-long");
	result.longAlertFrame = (await app.frames("pane.dialog"))["pane.dialog"];
	await answer(app, "OK");

	// Under a dialog the page takes no press, where the sidebar and the
	// toolbar still take theirs; Esc and Return reach the dialog wherever the
	// window's keys are, the address included (the Swift's key monitor).
	await page.evaluate("runConfirm()");
	await waitForSheet(app, "javaScript");
	const frames = await app.frames("pane.");

	const [cardX = 0, cardY = 0, cardWidth = 0, cardHeight = 0] =
		frames["pane.dialog"] ?? [];

	const [reloadX = 0, reloadY = 0] = frames["pane.toolbar.reload"] ?? [];
	const [rowX = 0, rowY = 0] = frames[`pane.tab.${tab}`] ?? [];

	const hits = {
		page: await app.hitTest(cardX + cardWidth + 100, cardY + cardHeight + 120),
		card: await app.hitTest(cardX + 10, cardY + 10),
		reload: await app.hitTest(reloadX + 8, reloadY + 8),
		row: await app.hitTest(rowX + 30, rowY + 10),
	};

	expect(
		hits.page.target === "views" &&
			hits.card.view === "pane.dialog" &&
			hits.reload.view === "pane.toolbar.reload" &&
			hits.row.view.startsWith("pane.tab"),
		`under the dialog: ${JSON.stringify(hits)}`,
	);

	// A click into the address, which the toolbar still takes.
	await app.click("pane.toolbar.address");

	await app.waitFor(
		"the address",
		(state) => state.focus === "pane.toolbar.address",
	);

	await app.send("dispatchKey", { key: "\u001b" });

	const escaped = await pageResult<{ value: boolean }>((expression) =>
		page.evaluate(expression),
	);

	expect(escaped.value === false, `Esc from the address gave ${escaped.value}`);
	await app.waitFor("the confirm gone", (state) => state.sheet === null);
	result.underTheDialog = hits;

	// A background tab's dialog waits in line, and shows once its tab is
	// chosen.
	const { tab: behind, page: behindPage } = await app.openPage(
		work,
		fixtures.url("dialogs.html?behind"),
	);

	await app.send("select", { tab });
	await app.waitFor("the first tab", (state) => state.activeTabID === tab);
	await behindPage.evaluate("runAlert()");

	await app.waitFor(
		"the alert in line",
		(state) => state.pendingRequests === 1 && state.sheet === null,
	);

	await app.send("select", { tab: behind });
	const waited = await waitForSheet(app, "javaScript");
	expect(waited.tabID === behind, `the alert of ${waited.tabID} showed`);
	await answer(app, "OK");

	// The switcher over a page's dialog takes Esc first, Control held; the
	// dialog's then, which a confirm answers with false (KeyRoutingTests.
	// swift's dialogOrder).
	await behindPage.evaluate("runConfirm()");
	await waitForSheet(app, "javaScript");
	await app.send("key", { key: "\t", modifiers: ["control"] });

	await app.waitFor(
		"the switcher over the dialog",
		(state) => state.switcher.visible,
	);

	await app.send(context.foreground ? "key" : "dispatchKey", {
		key: "\u001b",
		modifiers: ["control"],
	});

	const underSwitcher = await app.waitFor(
		"the switcher gone, the dialog still there",
		(state) => !state.switcher.visible && state.sheet?.kind === "javaScript",
	);

	await (context.foreground
		? app.send("releaseModifiers")
		: app.send("dispatchKey", { key: "control", phase: "release" }));

	expect(
		underSwitcher.activeTabID === behind,
		"Esc in the switcher switched tabs",
	);

	await pressWindowKey(context, app, "\u001b");

	const dismissed = await pageResult<{ value: boolean }>((expression) =>
		behindPage.evaluate(expression),
	);

	expect(
		dismissed.value === false,
		`Esc after the switcher gave ${dismissed.value}`,
	);

	await app.waitFor("the confirm gone", (state) => state.sheet === null);
	result.switcherOverDialog = "the switcher, then the dialog";
	await app.send("closeTab", { tab: behind });
	await app.send("select", { tab });

	// Where Chrome's own tab-modal dialogs show: at the top of the page,
	// centered on it, as big as the page at most.
	const host = (await app.send("modalHost", { width: 260, height: 100 }))
		.result as {
		position: number[];
		maximum: number[];
		page: number[];
		hostView: boolean;
	};

	const [pageX = 0, pageY = 0, pageWidth = 0, pageHeight = 0] = host.page;

	expect(
		host.hostView &&
			host.position[0] === pageX + Math.floor((pageWidth - 260) / 2) &&
			host.position[1] === pageY &&
			host.maximum[0] === pageWidth &&
			host.maximum[1] === pageHeight,
		`the tab-modal host: ${JSON.stringify(host)}`,
	);

	result.modalHost = host;

	// Chrome asks before unloading only a page the user interacted with.
	await page.evaluate("armBeforeUnload()");
	await clickAt((method, params) => page.send(method, params), 20, 20);

	await app.send("reload");
	await waitForSheet(app, "javaScript");
	await render(app, "dialog-reload-site");
	await answer(app, "Cancel");

	await app.send("closeTab", { tab });
	await waitForSheet(app, "javaScript");
	await render(app, "dialog-leave-site");
	await app.send("clickSheetButton", { title: "Leave" });
	await app.waitFor("the tab closed", (state) => !tabOf(state, tab));

	const echo = fixtures.url("echo");

	const { tab: form, page: formPage } = await app.openPage(
		work,
		fixtures.url("form.html"),
	);

	await formPage.evaluate("document.getElementById('form').submit()");

	await app.waitFor(
		"the posted page",
		(state) => tabOf(state, form)?.url === echo && !tabOf(state, form)?.loading,
	);

	await app.send("reload");
	await waitForSheet(app, "javaScript");
	await render(app, "dialog-form-resubmission");
	await answer(app, "Cancel");
	await app.send("closeTab", { tab: form });

	// What a site asks of the user: a permission, another app, and a sign-in,
	// asked again once refused.
	const { tab: asking, page: permissions } = await app.openPage(
		work,
		fixtures.url("permissions.html"),
	);

	await permissions.gesture("askNotifications()");
	await waitForSheet(app, "permission");
	await render(app, "dialog-permission");

	result.permissionAccessibility = (
		await app.send("dialogAccessibility")
	).result;

	await answer(app, "Don’t Allow");
	await app.send("closeTab", { tab: asking });

	// Cancel only: Open would start Maps on this Mac.
	const { tab: linking, page: external } = await app.openPage(
		work,
		fixtures.url("external.html"),
	);

	const link = await external.evaluate<{ x: number; y: number }>(
		"(() => { const r = document.getElementById('mail').getBoundingClientRect(); return { x: r.left + 5, y: r.top + 5 }; })()",
	);

	await clickAt(
		(method, params) => external.send(method, params),
		link.x,
		link.y,
	);

	await waitForSheet(app, "externalProtocol");
	await render(app, "dialog-external-app");
	await answer(app, "Cancel");
	await app.send("closeTab", { tab: linking });

	const signIn = await app.openTab(work, fixtures.url("auth"));
	await waitForSheet(app, "authentication");
	await render(app, "dialog-sign-in");
	result.signInAccessibility = (await app.send("dialogAccessibility")).result;

	// The server turns the password down and asks again: the Swift's dialog
	// has no error line, it asks again, empty.
	await app.send("clickSheetButton", {
		title: "Sign In",
		texts: ["pane", "wrong"],
	});

	await app
		.waitFor("the sign-in sent", (state) => state.sheet === null, 3_000)
		.catch(() => {});

	await waitForSheet(app, "authentication");
	await render(app, "dialog-sign-in-refused");
	await answer(app, "Cancel");

	await app.waitFor(
		"the refusal",
		(state) => tabOf(state, signIn)?.loading === false,
	);

	await app.send("closeTab", { tab: signIn });

	// A page in an endless loop: a key typed into it goes unanswered, and
	// the hang monitor asks.
	const hung = await app.openTab(work, fixtures.url("hang.html?auto"));

	await app.waitFor(
		"the page hung",
		(state) => tabOf(state, hung)?.title === "Hanging",
	);

	await poll(
		"Page Unresponsive",
		async () => {
			await Bun.sleep(500);
			await app.send("typeInPage", { key: "a" });

			return waitForSheet(app, "unresponsive", 25_000).catch(() => null);
		},
		75_000,
		0,
	);

	await render(app, "dialog-hung-page");
	await answer(app, "Exit Page");
	await app.send("closeTab", { tab: hung });

	// The first frames, from Chrome's hook to the frame on the screen.
	const round = (value: number) => Math.round(value * 10) / 10;

	const summary = (values: number[]) => {
		const sorted = [...values].sort((a, b) => a - b);
		const { median } = stats(sorted);

		return {
			medianMs: round(median),
			p10Ms: round(sorted[Math.floor(sorted.length * 0.1)] ?? median),
			p90Ms: round(sorted[Math.floor(sorted.length * 0.9)] ?? median),
			runs: values.length,
		};
	};

	const { page: timed } = await app.openPage(
		work,
		fixtures.url("dialogs.html?timing"),
	);

	const alerts = await signpostFrameIntervals(async () => {
		for (let round = 0; round < ROUNDS; round++) {
			await timed.evaluate("runAlert()");
			await waitForSheet(app, "javaScript");
			await Bun.sleep(250);
			await app.send("dispatchKey", { key: "\r" });
			await pageResult((expression) => timed.evaluate(expression));
			await app.waitFor("the alert gone", (state) => state.sheet === null);
			await Bun.sleep(200);
		}

		return app.browser.pid;
	});

	// A prompt dismissed (Esc) may ask again, twice before Chrome's embargo:
	// four permissions, from two origins, but notifications, which the
	// render above turned down for the first.
	const asksOf = (origin: string) => [
		...(origin === "localhost" ? ["askNotifications()"] : []),
		"askIdle()",
		"askWindowManagement()",
		"askFonts()",
	];

	const prompts = await signpostFrameIntervals(async () => {
		for (const origin of ["127.0.0.1", "localhost"]) {
			const url = fixtures
				.url("permissions.html?timing")
				.replace("127.0.0.1", origin);

			const { tab: prompting, page: asker } = await app.openPage(work, url);

			for (let round = 0; round < 2; round++) {
				for (const ask of asksOf(origin)) {
					await asker.gesture(ask);
					await waitForSheet(app, "permission");
					await Bun.sleep(250);
					await app.send("dispatchKey", { key: "\u001b" });
					await pageResult((expression) => asker.evaluate(expression));
					await app.waitFor("the prompt gone", (state) => state.sheet === null);
					await Bun.sleep(200);
				}
			}

			await app.send("closeTab", { tab: prompting });
		}

		return app.browser.pid;
	});

	const timings: Record<string, unknown> = {};

	for (const [name, intervals] of [
		["Page dialog", alerts["Page dialog"] ?? []],
		["Permission prompt", prompts["Permission prompt"] ?? []],
	] as const) {
		expect(intervals.length > 0, `${name}: no intervals`);

		timings[name] = {
			toScreen: summary(intervals.map((value) => value.ms)),
			toCommit: summary(
				intervals.flatMap((value) =>
					value.committedMs === undefined ? [] : [value.committedMs],
				),
			),
		};
	}

	result.timings = timings;

	// The proxy's sign-in, from a launch of its own behind a proxy that asks.
	const proxy = Bun.serve({
		hostname: "127.0.0.1",
		port: 0,
		fetch: () =>
			new Response("Proxy sign in required", {
				status: 407,
				headers: { "Proxy-Authenticate": 'Basic realm="Pane proxy"' },
			}),
	});

	try {
		const proxied = await launchPane(context, {
			args: [`--proxy-server=http://127.0.0.1:${proxy.port}`],
		});

		const through = await proxied.createProfile("Work", "BLUE");
		await proxied.openTab(through, "http://pane-proxy.test/");
		await waitForSheet(proxied, "authentication");
		await render(proxied, "dialog-sign-in-proxy");
		await answer(proxied, "Cancel");
	} finally {
		proxy.stop(true);
	}

	return result;
};

export default viewsDialogs;
