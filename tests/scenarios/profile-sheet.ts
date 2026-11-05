import { join } from "node:path";
import { $ } from "bun";
import {
	canDriveUI,
	clickButton,
	clickControl,
	mainWindowOf,
	readControl,
	typeIntoField,
	windowState,
} from "../lib/cua";
import { type AppState, type PaneApp, tabOf } from "../lib/pane";
import { poll } from "../lib/poll";
import { startHttpProxy } from "../lib/proxies";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// The profile sheet (ADR 0120), filled as a user would: presses and typing
// through the window's accessibility, never the pointer. A profile on the
// Mac's platform claims the Mac's real GPU, the very strings WebGL reports in
// a profile without a fingerprint; a rename keeps the fingerprint; another
// platform gets that platform's preset, the warning, and reaches the profile's
// open tab; a proxy string pasted into the address fills the fields, and its
// test answers through the proxy (proxy checks its password at rest); Save
// history reaches the engine (profile-history checks what the engine then
// does); a fingerprint follows the computer's time zone until the menu names
// another, which reaches the open tab; and turning spoofing off clears the
// fingerprint.

type Saved = {
	id: string;
	name: string;
	savesHistory: boolean;
	fingerprint: {
		platform: string;
		timeZone: string;
		webgl: { vendor: string; renderer: string } | null;
	} | null;
	proxy: {
		proxyType: string;
		host: string;
		port: number;
		username: string | null;
		password: string | null;
	} | null;
};

const CREDENTIALS = { username: "pane", password: "proxy-secret" };

async function saved(app: PaneApp, id: string): Promise<Saved> {
	return (await app.send<Saved>("readProfile", { profile: id })).result;
}

function newProfile(state: AppState, known: string[]): string | undefined {
	return state.profiles.find((profile) => !known.includes(profile.id))?.id;
}

const profileSheet: Scenario = async (context) => {
	// The sheet is filled and pressed through accessibility.
	if (!(await canDriveUI())) {
		context.defer("the profile sheet, driven through accessibility");

		return { skipped: "the profile sheet is driven through accessibility" };
	}

	const fixturePort = Number(new URL(context.fixtures.origin).port);
	const proxy = await startHttpProxy(fixturePort, CREDENTIALS);

	try {
		const { app } = await launchTraced(context, "profile-sheet", {
			env: { PANE_PROXY_TEST_URL: "http://check.pane.test/ip" },
		});

		const pid = app.browser.pid;
		const window = await mainWindowOf(pid);

		// What WebGL reports on this Mac, in a profile without a fingerprint.
		const plain = await app.createProfile("Plain", "ZINC");
		const plainURL = context.fixtures.url("nav-a.html?plain");

		const plainPage = await app.page(
			await app.openTab(plain, plainURL),
			plainURL,
		);

		const realWebGL = await plainPage.evaluate<{
			vendor: string;
			renderer: string;
		}>(
			`(() => { const gl = document.createElement("canvas").getContext("webgl"); const info = gl.getExtension("WEBGL_debug_renderer_info"); return { vendor: gl.getParameter(info.UNMASKED_VENDOR_WEBGL), renderer: gl.getParameter(info.UNMASKED_RENDERER_WEBGL) }; })()`,
		);

		const host = (await app.state()).hostWebGL;

		expect(
			host?.vendor === realWebGL.vendor &&
				host?.renderer === realWebGL.renderer,
			`the engine's GPU strings ${JSON.stringify(host)} are not WebGL's ${JSON.stringify(realWebGL)}`,
		);

		// VoiceOver's names: the open profile's row reads its tabs and state.
		const tree = async (name: string) =>
			(await windowState(pid, window, join(context.out, `${name}.png`)))
				.tree_markdown;

		// The sheet as it is, beside Paper's boards, in both themes.
		const render = async (name: string) => {
			for (const theme of ["light", "dark"]) {
				await app.send("theme", { theme });
				await Bun.sleep(300);

				await app.send("render", {
					path: join(context.out, `${name}-${theme}.png`),
				});
			}

			await app.send("theme", { theme: "system" });
		};

		const sidebar = await tree("profile-sheet-a11y-sidebar");

		// New profile: spoofing on, the Mac's platform, by default.
		let known = (await app.state()).profiles.map((profile) => profile.id);
		await clickButton(pid, window, "New");

		await app.waitFor(
			"the new profile sheet",
			(state) => state.profileSheet === "new",
		);

		await typeIntoField(pid, window, "pane.profile.name", "Mac claim");
		await render("profile-sheet-new");

		// The sheet's fields read their labels, not their placeholders, and
		// the sheet is modal: the sidebar under it is out of reach.
		const sheet = await tree("profile-sheet-a11y-sheet");

		const named = {
			plainRow: /AXButton = "1 tab, expanded" \(Plain\)/.test(sidebar),
			name: /AXTextField[^\n]*\(Name\)[^\n]*id=pane\.profile\.name/.test(sheet),
			group: /AXTextField[^\n]*\(Group\)/.test(sheet),
			history: /AX(CheckBox|Switch)[^\n]*\(Save history\)/.test(sheet),
			spoofing: /AX(CheckBox|Switch)[^\n]*\(Spoof fingerprint\)/.test(sheet),
			platform: /AXButton[^\n]*\(macOS\)/.test(sheet),
			sidebarHidden: !/AXButton[^\n]*\(Plain\)/.test(sheet),
		};

		expect(
			Object.values(named).every(Boolean),
			`accessibility names: ${JSON.stringify(named)}`,
		);

		await clickButton(pid, window, "Create");

		const created = await app.waitFor(
			"the profile created",
			(state) => !!newProfile(state, known) && state.profileSheet === null,
		);

		const mac = newProfile(created, known) ?? "";
		const macSaved = await saved(app, mac);

		expect(
			macSaved.savesHistory === true &&
				macSaved.fingerprint?.platform === "MACOS" &&
				macSaved.fingerprint.webgl?.vendor === realWebGL.vendor &&
				macSaved.fingerprint.webgl?.renderer === realWebGL.renderer,
			`the Mac profile's fingerprint: ${JSON.stringify(macSaved.fingerprint)}`,
		);

		// A page of it, to see the fingerprint change reach it.
		const fingerprintURL = context.fixtures.url("fingerprint-frame.html?sheet");
		const macTab = await app.openTab(mac, fingerprintURL);

		const before = await (
			await app.page(macTab, fingerprintURL)
		).evaluate<string>("navigator.platform");

		// A rename keeps the fingerprint.
		await app.send("openProfileSheet", { profile: mac });
		await app.waitFor("the edit sheet", (state) => state.profileSheet === mac);
		// The field selects its text as it takes focus: typing replaces it.
		await typeIntoField(pid, window, "pane.profile.name", "Mac claim renamed");
		await clickButton(pid, window, "Save");

		await app.waitFor(
			"the sheet closed",
			(state) => state.profileSheet === null,
		);

		const renamed = await saved(app, mac);

		expect(
			renamed.name === "Mac claim renamed" &&
				JSON.stringify(renamed.fingerprint) ===
					JSON.stringify(macSaved.fingerprint),
			`the rename changed ${JSON.stringify(renamed)}`,
		);

		// Another platform: its preset, the warning, and the open tab follows.
		// Save history off with it, which the open profile's engine takes at
		// once.
		await app.send("openProfileSheet", { profile: mac });
		await app.waitFor("the edit sheet", (state) => state.profileSheet === mac);
		await clickButton(pid, window, "Windows");

		await clickControl(pid, window, {
			title: "Save history",
			role: /AX(CheckBox|Switch)/,
		});

		await Bun.sleep(300);
		await render("profile-sheet-cross-os");
		await clickButton(pid, window, "Save");

		await app.waitFor(
			"the sheet closed",
			(state) => state.profileSheet === null,
		);

		const windows = await saved(app, mac);

		expect(
			windows.fingerprint?.platform === "WINDOWS" &&
				windows.fingerprint.webgl?.vendor === "Google Inc. (NVIDIA)",
			`the Windows fingerprint: ${JSON.stringify(windows.fingerprint)}`,
		);

		const historyOff = await app.waitFor(
			"the engine told of Save history",
			(state) =>
				state.profiles.find((each) => each.id === mac)?.engineSavesHistory ===
				false,
		);

		expect(
			windows.savesHistory === false,
			`Save history off saved ${windows.savesHistory}`,
		);

		await app.send("select", { tab: macTab });

		await app.waitFor(
			"the tab back",
			(state) =>
				!!tabOf(state, macTab)?.loaded && !tabOf(state, macTab)?.loading,
		);

		const after = await (
			await app.page(macTab, fingerprintURL)
		).evaluate<string>("navigator.platform");

		expect(
			before === "MacIntel" && after === "Win32",
			`the open tab saw ${before}, then ${after}`,
		);

		// The computer's time zone, until the menu names another. The menu's
		// titles carry today's offset, as Intl spells it but for its minus sign.
		const macZone = Intl.DateTimeFormat().resolvedOptions().timeZone;

		const pageZone = async () =>
			(await app.page(macTab, fingerprintURL)).evaluate<string>(
				"Intl.DateTimeFormat().resolvedOptions().timeZone",
			);

		const followed = await pageZone();

		const offset = new Intl.DateTimeFormat("en-US", {
			timeZone: "Asia/Tokyo",
			timeZoneName: "shortOffset",
		})
			.formatToParts(new Date())
			.find((part) => part.type === "timeZoneName")?.value;

		const chooseZone = async (path: string[], shows: string) => {
			await app.send("openProfileSheet", { profile: mac });

			await app.waitFor(
				"the edit sheet",
				(state) => state.profileSheet === mac,
			);

			await Bun.sleep(400);

			// Pressed through accessibility in a window that may be on another
			// Space, a menu or its submenu does not always open or take the press:
			// again from the trigger, until the trigger shows the choice.
			await poll(
				`the menu's ${path.join(" > ")}`,
				async () => {
					try {
						await clickControl(pid, window, {
							title: "Time zone",
							role: /AXMenuButton/,
						});

						for (const title of path) {
							await clickControl(pid, "any", { title, role: /AXMenuItem/ });
						}

						await Bun.sleep(200);

						const { value } = await readControl(
							pid,
							window,
							"Time zone",
							/AXMenuButton/,
						);

						return value === shows;
					} catch {
						return false;
					}
				},
				60_000,
				500,
			);

			await Bun.sleep(300);
			await clickButton(pid, window, "Save");

			await app.waitFor(
				"the sheet closed",
				(state) => state.profileSheet === null,
			);

			await app.send("select", { tab: macTab });

			await app.waitFor(
				"the tab back",
				(state) =>
					!!tabOf(state, macTab)?.loaded && !tabOf(state, macTab)?.loading,
			);
		};

		await chooseZone(["Asia", `Tokyo (${offset})`], "Tokyo");
		const chosen = await pageZone();
		const chosenSaved = (await saved(app, mac)).fingerprint?.timeZone;

		await chooseZone(["Automatic"], "Automatic");
		const followedAgain = await pageZone();
		const followedSaved = (await saved(app, mac)).fingerprint?.timeZone;

		const timeZone = {
			followed,
			chosen,
			chosenSaved,
			followedAgain,
			followedSaved,
		};

		expect(
			followed === macZone &&
				chosen === "Asia/Tokyo" &&
				chosenSaved === "Asia/Tokyo" &&
				followedAgain === macZone &&
				followedSaved === "",
			`the time zone, on a Mac in ${macZone}: ${JSON.stringify(timeZone)}`,
		);

		// The proxy: a string pasted into its address, its test, and Create.
		known = (await app.state()).profiles.map((profile) => profile.id);
		await clickButton(pid, window, "New");

		await app.waitFor(
			"the new profile sheet",
			(state) => state.profileSheet === "new",
		);

		await typeIntoField(pid, window, "pane.profile.name", "Proxied");

		await clickControl(pid, window, {
			title: "Proxy",
			role: /AX(CheckBox|Switch)/,
		});

		await typeIntoField(
			pid,
			window,
			"pane.profile.proxyHost",
			`127.0.0.1:${proxy.port}:${CREDENTIALS.username}:${CREDENTIALS.password}`,
		);

		await clickButton(pid, window, "Test connection");

		const tested = await poll(
			"the test's answer",
			async () => {
				const answer = await tree("profile-sheet-a11y-proxy");

				return answer.includes("Connected · 203.0.113.7") && answer;
			},
			10_000,
			200,
		);

		const fields = {
			host: /AXTextField[^\n]*"127\.0\.0\.1"[^\n]*\(Proxy host\)|AXTextField[^\n]*\(Proxy host\)[^\n]*127\.0\.0\.1/.test(
				tested,
			),
			port: new RegExp(
				`AXTextField[^\\n]*(\\(Proxy port\\)[^\\n]*${proxy.port}|${proxy.port}[^\\n]*\\(Proxy port\\))`,
			).test(tested),
		};

		expect(
			Object.values(fields).every(Boolean),
			`the pasted proxy's fields: ${JSON.stringify(fields)}`,
		);

		await render("profile-sheet-proxy");

		// The form runs past the body here: it ends hard at the rules, which show
		// in both themes, for the eye to check in the window as the screen shows
		// it.
		const nameField = await readControl(pid, window, "Name", /AXTextField/);

		await app.send("scrollWindow", {
			x: nameField.x + 40,
			y: nameField.y + 200,
			dy: -12,
		});

		await Bun.sleep(300);

		for (const theme of ["light", "dark"]) {
			await app.send("theme", { theme });
			await Bun.sleep(300);

			await $`/usr/sbin/screencapture -x -o -l ${window} ${join(context.out, `profile-sheet-scrolled-${theme}.png`)}`
				.quiet()
				.nothrow();
		}

		await app.send("theme", { theme: "system" });

		await clickButton(pid, window, "Create");

		const proxied = await app.waitFor(
			"the proxied profile",
			(state) => !!newProfile(state, known) && state.profileSheet === null,
		);

		const proxiedID = newProfile(proxied, known) ?? "";
		const proxiedSaved = await saved(app, proxiedID);

		const savedProxy = proxiedSaved.proxy;

		const shownProxy = {
			...savedProxy,
			password: savedProxy?.password ? "(set)" : null,
		};

		expect(
			savedProxy?.proxyType === "HTTP" &&
				savedProxy.host === "127.0.0.1" &&
				savedProxy.port === proxy.port &&
				savedProxy.username === CREDENTIALS.username &&
				savedProxy.password === CREDENTIALS.password,
			`the saved proxy: ${JSON.stringify(shownProxy)}`,
		);

		expect(
			proxy.log.some(
				(entry) =>
					entry.target.includes("check.pane.test") &&
					entry.user === CREDENTIALS.username,
			),
			`the proxy test did not go through the proxy: ${JSON.stringify(proxy.log)}`,
		);

		// Spoofing off.
		await app.send("openProfileSheet", { profile: proxiedID });

		await app.waitFor(
			"the edit sheet",
			(state) => state.profileSheet === proxiedID,
		);

		// A real click on the switch's label, well clear of the switch: the
		// whole row toggles it, as a checkbox's label does. Once the sheet has
		// slid in (0.2 s) and its name field taken focus; two reads 100 ms apart
		// agree.
		await Bun.sleep(400);

		let spoofRow = await readControl(
			pid,
			window,
			"Spoof fingerprint",
			/AX(CheckBox|Switch)/,
		);

		await poll("the sheet to settle", async () => {
			await Bun.sleep(100);

			const again = await readControl(
				pid,
				window,
				"Spoof fingerprint",
				/AX(CheckBox|Switch)/,
			);

			const settled = again.x === spoofRow.x && again.y === spoofRow.y;
			spoofRow = again;

			return settled;
		});

		await app.send("clickWindow", {
			x: spoofRow.x + 30,
			y: spoofRow.y + spoofRow.h / 2,
		});

		// Until the switch reads its new value, or 3 s pass.
		const spoofAfterClick = await poll(
			"the switch to toggle",
			async () => {
				const { value } = await readControl(
					pid,
					window,
					"Spoof fingerprint",
					/AX(CheckBox|Switch)/,
				);

				return value !== spoofRow.value && value;
			},
			3_000,
			100,
		).catch(() => spoofRow.value);

		expect(
			spoofRow.value === "1" && spoofAfterClick === "0",
			`a click on the label at ${spoofRow.x + 30},${spoofRow.y + spoofRow.h / 2} left the switch at ${spoofAfterClick} (from ${spoofRow.value})`,
		);

		await clickButton(pid, window, "Save");

		await app.waitFor(
			"the sheet closed",
			(state) => state.profileSheet === null,
		);

		const unspoofed = await saved(app, proxiedID);

		expect(
			unspoofed.fingerprint === null,
			`spoofing off kept ${JSON.stringify(unspoofed.fingerprint)}`,
		);

		return {
			hostWebGL: host,
			webGLInAPlainProfile: realWebGL,
			macProfile: macSaved.fingerprint,
			accessibilityNames: named,
			renameKeptFingerprint: true,
			crossOS: {
				fingerprint: windows.fingerprint,
				openTab: { before, after },
			},
			timeZone,
			historyOff: {
				saved: windows.savesHistory,
				engine: historyOff.profiles.find((each) => each.id === mac)
					?.engineSavesHistory,
			},
			proxy: {
				saved: shownProxy,
				testedThrough: proxy.log.length,
			},
			spoofingOff: {
				byLabelClick: spoofAfterClick,
				saved: unspoofed.fingerprint,
			},
		};
	} finally {
		proxy.stop();
	}
};

export default profileSheet;
