import { join } from "node:path";
import {
	canDriveUI,
	clickButton,
	clickControl,
	mainWindowOf,
	readControl,
	typeIntoField,
	windowState,
} from "../lib/cua";
import { type AppState, launchPane, type PaneApp, tabOf } from "../lib/pane";
import { poll } from "../lib/poll";
import { startHttpProxy } from "../lib/proxies";
import { expect, type Scenario } from "../run";

// The profile sheet (ADR 0120), filled as a user would: presses and typing
// through the window's accessibility, never the pointer. A profile on the
// Mac's platform claims the Mac's real GPU, the very strings WebGL reports in
// a profile without a fingerprint; a rename keeps the fingerprint; another
// platform gets that platform's preset, the warning, and reaches the profile's
// open tab; a proxy string pasted into the address fills the fields, and its
// test answers through the proxy (proxy checks its password at rest); with no
// name, no host, one Chrome cannot read, or no valid port, it does not save, the
// field ringed in red and brought into view for want of words; Save
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

		return;
	}

	const fixturePort = Number(new URL(context.fixtures.origin).port);
	const proxy = await startHttpProxy(fixturePort, CREDENTIALS);

	try {
		const app = await launchPane(context, {
			env: { PANE_PROXY_TEST_URL: "http://check.pane.test/ip" },
		});

		const pid = app.browser.pid;
		const window = await mainWindowOf(pid);

		const newSheet = async () => {
			await clickButton(pid, window, "New");

			await app.waitFor(
				"the new profile sheet",
				(state) => state.profileSheet === "new",
			);
		};

		const editSheet = async (profile: string) => {
			await app.send("openProfileSheet", { profile });

			await app.waitFor(
				"the edit sheet",
				(state) => state.profileSheet === profile,
			);
		};

		const closeWith = async (button: string) => {
			await clickButton(pid, window, button);

			await app.waitFor(
				"the sheet closed",
				(state) => state.profileSheet === null,
			);
		};

		const tabBack = async (tab: string) => {
			await app.send("select", { tab });

			await app.waitFor(
				"the tab back",
				(state) => !!tabOf(state, tab)?.loaded && !tabOf(state, tab)?.loading,
			);
		};

		const proxySwitch = async () => {
			await clickControl(pid, window, {
				title: "Proxy",
				role: /AX(CheckBox|Switch)/,
			});

			await Bun.sleep(300);
		};

		// What WebGL reports on this Mac, in a profile without a fingerprint.
		const plain = await app.createProfile("Plain", "ZINC");
		const plainURL = context.fixtures.url("nav-a.html?plain");

		const { page: plainPage } = await app.openPage(plain, plainURL);

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
				await app.render(`${name}-${theme}.png`);
			}

			await app.send("theme", { theme: "system" });
		};

		const sidebar = await tree("profile-sheet-a11y-sidebar");

		// New profile: spoofing on, the Mac's platform, by default.
		let known = (await app.state()).profiles.map((profile) => profile.id);
		await newSheet();

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
			timeZone: /\bTime zone\b/.test(sheet),
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
		await editSheet(mac);
		// The field selects its text as it takes focus: typing replaces it.
		await typeIntoField(pid, window, "pane.profile.name", "Mac claim renamed");
		await closeWith("Save");

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
		await editSheet(mac);
		await clickButton(pid, window, "Windows");

		await clickControl(pid, window, {
			title: "Save history",
			role: /AX(CheckBox|Switch)/,
		});

		await Bun.sleep(300);
		await render("profile-sheet-cross-os");
		await closeWith("Save");

		const windows = await saved(app, mac);

		expect(
			windows.fingerprint?.platform === "WINDOWS" &&
				windows.fingerprint.webgl?.vendor === "Google Inc. (NVIDIA)",
			`the Windows fingerprint: ${JSON.stringify(windows.fingerprint)}`,
		);

		await app.waitFor(
			"the engine told of Save history",
			(state) =>
				state.profiles.find((each) => each.id === mac)?.engineSavesHistory ===
				false,
		);

		expect(
			windows.savesHistory === false,
			`Save history off saved ${windows.savesHistory}`,
		);

		await tabBack(macTab);

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
			await editSheet(mac);

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
			await closeWith("Save");
			await tabBack(macTab);
		};

		await chooseZone(["Asia", `Tokyo (${offset})`], "Tokyo");
		const chosen = await pageZone();
		const chosenSaved = (await saved(app, mac)).fingerprint?.timeZone;

		await chooseZone(["Automatic"], "Automatic");
		const followedAgain = await pageZone();
		const followedSaved = (await saved(app, mac)).fingerprint?.timeZone;

		expect(
			followed === macZone &&
				chosen === "Asia/Tokyo" &&
				chosenSaved === "Asia/Tokyo" &&
				followedAgain === macZone &&
				followedSaved === "",
			`the time zone, on a Mac in ${macZone}: ${JSON.stringify({ followed, chosen, chosenSaved, followedAgain, followedSaved })}`,
		);

		// The proxy: a string pasted into its address, its test, and Create.
		known = (await app.state()).profiles.map((profile) => profile.id);
		await newSheet();

		// With no name it does not save, and VoiceOver hears why.
		await clickButton(pid, window, "Create");
		await Bun.sleep(500);

		const noName = await app.state();

		expect(
			noName.profileSheet === "new" && !newProfile(noName, known),
			`no name saved: ${JSON.stringify(noName.profileSheet)}`,
		);

		const fieldLine = async (label: string, name: string) =>
			(await tree(`profile-sheet-a11y-${name}`))
				.split("\n")
				.find((line) => line.includes(`(${label})`)) ?? "";

		const nameLine = await fieldLine("Name", "name-missing");

		expect(
			nameLine.includes('help="Profile name is required"'),
			`no name: ${nameLine}`,
		);

		await render("profile-sheet-name-missing");
		await typeIntoField(pid, window, "pane.profile.name", "Proxied");

		await poll(
			"the name's problem to go",
			async () => {
				const line = await fieldLine("Name", "name-fixed");

				return !line.includes("help=") && line;
			},
			5_000,
			100,
		);

		// Another platform's warning takes the address past the body's end.
		await clickButton(pid, window, "Windows");

		await proxySwitch();

		const hostField = () =>
			readControl(pid, window, "Proxy host", /AXTextField/);

		// Where the body ends: the footer's rule, 16 over its buttons.
		const bodyEnd = async () =>
			(await readControl(pid, window, "Create", /AXButton/)).y - 17;

		const hostBefore = await hostField();
		const belowTheBody = hostBefore.y + hostBefore.h > (await bodyEnd());

		const proxiedAfresh = async () => {
			await closeWith("Cancel");
			await newSheet();
			await typeIntoField(pid, window, "pane.profile.name", "Proxied");
			await proxySwitch();
		};

		// Pressed with what cannot save: the sheet stays, and nothing is made.
		const refuses = async (
			name: string,
			message: string,
			label = "Proxy host",
		) => {
			await clickButton(pid, window, "Create");
			await Bun.sleep(500);
			const state = await app.state();

			expect(
				state.profileSheet === "new" && !newProfile(state, known),
				`${name} saved: ${JSON.stringify(state.profileSheet)}`,
			);

			const host = await hostField();

			const end = await bodyEnd();

			// In view, and clear of the rule.
			expect(
				host.y + host.h <= end - 8,
				`${name}: the host ends at ${host.y + host.h}, the body at ${end} (started below it: ${belowTheBody})`,
			);

			// No words on the screen: VoiceOver hears why.
			const line = await fieldLine(label, name);

			expect(line.includes(`help="${message}"`), `${name}: ${line}`);

			await render(`profile-sheet-${name}`);
		};

		await refuses("host-missing", "Proxy host is required");

		await typeIntoField(
			pid,
			window,
			"pane.profile.proxyHost",
			"http://127.0.0.1",
		);

		await refuses("host-unreadable", "Host must be a name or an IP address");

		// Afresh: the field holds what was typed, and takes more after it.
		await proxiedAfresh();
		await refuses("host-missing-again", "Proxy host is required");

		// A host, and a port past the last one. The port's field selects its text
		// as it takes focus, so this replaces the 8080 it holds.
		await typeIntoField(pid, window, "pane.profile.proxyHost", "127.0.0.1");
		await typeIntoField(pid, window, "pane.profile.proxyPort", "70000");

		await refuses(
			"port-invalid",
			"Port must be between 1 and 65535",
			"Proxy port",
		);

		// Afresh again, for the proxy string below.
		await proxiedAfresh();
		await refuses("host-missing-once-more", "Proxy host is required");

		await typeIntoField(
			pid,
			window,
			"pane.profile.proxyHost",
			`127.0.0.1:${proxy.port}:${CREDENTIALS.username}:${CREDENTIALS.password}`,
		);

		// A host now: the problem goes as it is typed.
		await poll(
			"the host's problem to go",
			async () => {
				const line = await fieldLine("Proxy host", "host-fixed");

				return !line.includes("help=") && line;
			},
			5_000,
			100,
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

			await Bun.$`/usr/sbin/screencapture -x -o -l ${window} ${join(context.out, `profile-sheet-scrolled-${theme}.png`)}`
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
		const savedProxy = (await saved(app, proxiedID)).proxy;

		expect(
			savedProxy?.proxyType === "HTTP" &&
				savedProxy.host === "127.0.0.1" &&
				savedProxy.port === proxy.port &&
				savedProxy.username === CREDENTIALS.username &&
				savedProxy.password === CREDENTIALS.password,
			`the saved proxy: ${JSON.stringify({ ...savedProxy, password: savedProxy?.password ? "(set)" : null })}`,
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
		await editSheet(proxiedID);

		// A real click on the switch's label, well clear of the switch: the
		// whole row toggles it, as a checkbox's label does. Once the sheet has
		// slid in (0.2 s) and its name field taken focus; two reads 100 ms apart
		// agree.
		await Bun.sleep(400);

		const spoofSwitch = () =>
			readControl(pid, window, "Spoof fingerprint", /AX(CheckBox|Switch)/);

		let spoofRow = await spoofSwitch();

		await poll("the sheet to settle", async () => {
			await Bun.sleep(100);

			const again = await spoofSwitch();

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
				const { value } = await spoofSwitch();

				return value !== spoofRow.value && value;
			},
			3_000,
			100,
		).catch(() => spoofRow.value);

		expect(
			spoofRow.value === "1" && spoofAfterClick === "0",
			`a click on the label at ${spoofRow.x + 30},${spoofRow.y + spoofRow.h / 2} left the switch at ${spoofAfterClick} (from ${spoofRow.value})`,
		);

		// With spoofing off sites see the Mac's zone with the rest of the Mac.
		expect(
			!/\bTime zone\b/.test(await tree("profile-sheet-a11y-spoof-off")),
			"the time zone's row still shows with spoofing off",
		);

		await closeWith("Save");

		const unspoofed = await saved(app, proxiedID);

		expect(
			unspoofed.fingerprint === null,
			`spoofing off kept ${JSON.stringify(unspoofed.fingerprint)}`,
		);
	} finally {
		proxy.stop();
	}
};

export default profileSheet;
