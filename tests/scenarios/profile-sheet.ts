import { join } from "node:path";
import { type AppState, launchPane, type PaneApp, tabOf } from "../lib/pane";
import { bySystem } from "../lib/platform";
import { poll, pollValue } from "../lib/poll";
import { startHttpProxy } from "../lib/proxies";
import { captureWindow } from "../lib/tools";
import { expect, type Scenario } from "../run";

// The profile sheet (ADR 0120), filled as a user would: real clicks on its
// controls, and text set in its fields as a screen reader sets it, read back
// from Views' own accessibility. A profile on the Mac's platform claims the
// Mac's real GPU, the very strings WebGL reports in a profile without a
// fingerprint; a rename keeps the fingerprint; another platform gets that
// platform's preset, the warning, and reaches the profile's open tab; a proxy
// string pasted into the address fills the fields, and its test answers
// through the proxy (proxy checks its password at rest); with no name, no
// host, one Chrome cannot read, or no valid port, it does not save, the field
// ringed in red and brought into view for want of words; Save history reaches
// the engine (profile-history checks what the engine then does); a
// fingerprint follows the computer's time zone until the menu names another,
// which reaches the open tab; and turning spoofing off clears the
// fingerprint. The sheet shows no Group (the user's answer, 2026-11-11).

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

// What VoiceOver reads of a control of the sheet.
type Read = {
	role: string;
	name: string;
	value: string;
	description: string;
	checked: boolean;
	selected: boolean;
	modal: boolean;
};

const CREDENTIALS = { username: "pane", password: "proxy-secret" };

// The computer's platform, which a new profile claims with the computer's
// own GPU, and another, with its preset's (FingerprintPreset): each's card,
// its name in the fingerprint, and what navigator.platform says of it.
const PLATFORMS = bySystem({
	mac: {
		own: {
			card: "macos",
			title: "macOS",
			name: "MACOS",
			navigator: "MacIntel",
		},
		other: {
			card: "windows",
			name: "WINDOWS",
			navigator: "Win32",
			vendor: "Google Inc. (NVIDIA)",
		},
	},
	windows: {
		own: {
			card: "windows",
			title: "Windows",
			name: "WINDOWS",
			navigator: "Win32",
		},
		other: {
			card: "macos",
			name: "MACOS",
			navigator: "MacIntel",
			vendor: "Google Inc. (Apple)",
		},
	},
});

async function saved(app: PaneApp, id: string): Promise<Saved> {
	return (await app.send<Saved>("readProfile", { profile: id })).result;
}

function newProfile(state: AppState, known: string[]): string | undefined {
	return state.profiles.find((profile) => !known.includes(profile.id))?.id;
}

const profileSheet: Scenario = async (context) => {
	const fixturePort = Number(new URL(context.fixtures.origin).port);
	const proxy = await startHttpProxy(fixturePort, CREDENTIALS);

	try {
		const app = await launchPane(context, {
			env: { PANE_PROXY_TEST_URL: "http://check.pane.test/ip" },
		});

		// The sheet's controls, and the sidebar's profiles, as the screen reader
		// reads them, by the Swift's names.
		const read = async () =>
			(
				await app.send<Record<string, Read>>("accessibility", {
					prefix: "pane.profile",
				})
			).result;

		const control = async (name: string) => {
			const found = (await read())[name];

			if (!found) {
				throw new Error(`the sheet shows no ${name}`);
			}

			return found;
		};

		// Where a control shows: x, y, width, height.
		const frame = async (name: string) => {
			const [x = 0, y = 0, w = 0, h = 0] = (await app.frames(name))[name] ?? [];

			return { x, y, w, h };
		};

		const type = (field: string, text: string) =>
			app.send("sheetField", { field, text });

		// A real click, its place kept: the pointer rests there after, as a
		// hand's does, until it leaves (pointerAway).
		let pressed: string | undefined;

		const press = async (name: string) => {
			pressed = name;
			await app.click(name);
		};

		// The pointer off what it last pressed, onto the sheet's title, where
		// nothing answers a hover: the Swift's presses had no pointer, nor have
		// Paper's boards.
		const pointerAway = async () => {
			const sheet = await frame("pane.profileSheet");

			if (sheet.w === 0) {
				return;
			}

			if (pressed) {
				const last = await frame(pressed);

				if (last.w > 0) {
					await app.send("hoverWindow", {
						x: last.x + last.w / 2,
						y: last.y + last.h / 2,
					});
				}
			}

			await app.send("hoverWindow", { x: sheet.x + 32, y: sheet.y + 28 });
		};

		const newSheet = async () => {
			// A hand is over New before it presses it, so the window knows the
			// pointer is there and the sheet's scrim takes it from New as it
			// shows (pointerAway): a click with no move before it left New lit
			// under the scrim in every render.
			const button = await frame("pane.sidebar.new");

			await app.send("hoverWindow", {
				x: button.x + button.w / 2,
				y: button.y + button.h / 2,
			});

			await press("pane.sidebar.new");

			await app.waitFor(
				"the new profile sheet",
				(state) =>
					state.profileSheet === "new" && state.focus === "pane.profile.name",
			);

			// Once it has slid in (0.2 s): a click lands where it rests.
			await Bun.sleep(300);
		};

		const editSheet = async (profile: string) => {
			await app.send("openProfileSheet", { profile });

			await app.waitFor(
				"the edit sheet",
				(state) =>
					state.profileSheet === profile && state.focus === "pane.profile.name",
			);

			// Once it has slid in (0.2 s).
			await Bun.sleep(300);
		};

		const closeWith = async (button: string) => {
			await press(button);

			await app.waitFor(
				"the sheet closed",
				(state) => state.profileSheet === null,
			);

			// Once it has slid out (0.2 s), as a hand takes longer than that to
			// reach New.
			await Bun.sleep(300);
		};

		const tabBack = async (tab: string) => {
			await app.send("select", { tab });

			await app.waitFor(
				"the tab back",
				(state) => !!tabOf(state, tab)?.loaded && !tabOf(state, tab)?.loading,
			);
		};

		// A switch's row, by a real click, once what it shows or hides has
		// settled (Motion.toggle).
		const toggle = async (name: string) => {
			await press(name);
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

		// The sheet as it is, beside Paper's boards and the Swift's renders,
		// in both themes.
		// After a scroll, long enough for Views' overlay scroller to go (it
		// shows on any scroll, a reveal's too, and hides 0.5 s after).
		const afterScroll = 1_200;

		const render = async (name: string, settleMs = 300) => {
			await pointerAway();

			for (const theme of ["light", "dark"]) {
				await app.send("theme", { theme });
				await app.render(`${name}-${theme}.png`, settleMs);
			}

			await app.send("theme", { theme: "system" });
		};

		// VoiceOver's names: the open profile's row reads its tabs and state.
		const plainRow = (await read())[`pane.profile.${plain}`];

		// New profile: spoofing on, the computer's platform, by default.
		let known = (await app.state()).profiles.map((profile) => profile.id);
		await newSheet();

		await type("name", "Mac claim");
		await render("profile-sheet-new");

		// The sheet's fields read their labels, not their placeholders; the
		// sheet is a modal dialog; and it has no Group.
		const names = await read();

		const named = {
			plainRow:
				plainRow?.name === "Plain" && plainRow?.value === "1 tab, expanded",
			sheet:
				names["pane.profileSheet"]?.role === "dialog" &&
				names["pane.profileSheet"]?.name === "New profile" &&
				names["pane.profileSheet"]?.modal === true,
			name: names["pane.profile.name"]?.name === "Name",
			history:
				names["pane.profile.history"]?.role === "switch" &&
				names["pane.profile.history"]?.name === "Save history",
			spoofing: names["pane.profile.spoof"]?.name === "Spoof fingerprint",
			platform:
				names[`pane.profile.platform.${PLATFORMS.own.card}`]?.name ===
					PLATFORMS.own.title &&
				names[`pane.profile.platform.${PLATFORMS.own.card}`]?.selected === true,
			timeZone: names["pane.profile.timeZone"]?.name === "Time zone",
			color:
				names["pane.profile.color.blue"]?.name === "Blue" &&
				names["pane.profile.color.blue"]?.selected === true,
			noGroup: !Object.values(names).some((each) => each.name === "Group"),
		};

		expect(
			Object.values(named).every(Boolean),
			`accessibility names: ${JSON.stringify(named)}`,
		);

		await press("pane.profile.save");

		const created = await app.waitFor(
			"the profile created",
			(state) => !!newProfile(state, known) && state.profileSheet === null,
		);

		const mac = newProfile(created, known) ?? "";
		const macSaved = await saved(app, mac);

		expect(
			macSaved.savesHistory === true &&
				macSaved.fingerprint?.platform === PLATFORMS.own.name &&
				macSaved.fingerprint.webgl?.vendor === realWebGL.vendor &&
				macSaved.fingerprint.webgl?.renderer === realWebGL.renderer,
			`the computer's platform's fingerprint: ${JSON.stringify(macSaved.fingerprint)}`,
		);

		// A page of it, to see the fingerprint change reach it.
		const fingerprintURL = context.fixtures.url("fingerprint-frame.html?sheet");
		const macTab = await app.openTab(mac, fingerprintURL);

		const before = await (
			await app.page(macTab, fingerprintURL)
		).evaluate<string>("navigator.platform");

		// A rename keeps the fingerprint.
		await editSheet(mac);
		await type("name", "Mac claim renamed");
		await closeWith("pane.profile.save");

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
		await press(`pane.profile.platform.${PLATFORMS.other.card}`);
		await toggle("pane.profile.history");
		await render("profile-sheet-cross-os");

		const crossOS = await read();

		expect(
			crossOS["pane.profile.warning"] !== undefined &&
				crossOS[`pane.profile.platform.${PLATFORMS.other.card}`]?.selected ===
					true &&
				crossOS["pane.profile.history"]?.checked === false,
			`another platform: ${JSON.stringify({ warning: crossOS["pane.profile.warning"], other: crossOS[`pane.profile.platform.${PLATFORMS.other.card}`], history: crossOS["pane.profile.history"] })}`,
		);

		await closeWith("pane.profile.save");

		const windows = await saved(app, mac);

		expect(
			windows.fingerprint?.platform === PLATFORMS.other.name &&
				windows.fingerprint.webgl?.vendor === PLATFORMS.other.vendor,
			`the other platform's fingerprint: ${JSON.stringify(windows.fingerprint)}`,
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
			before === PLATFORMS.own.navigator && after === PLATFORMS.other.navigator,
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
			.find((part) => part.type === "timeZoneName")
			?.value.replace("-", "−");

		const chooseZone = async (path: string[], shows: string) => {
			await editSheet(mac);

			// The zones come off the UI thread as the sheet opens.
			await poll(
				"the zones listed",
				async () =>
					(
						await app.send<{ title: string }[]>("sheetMenu", {
							menu: "timeZone",
						})
					).result.some((item) => item.title === "Asia"),
				10_000,
				100,
			);

			await app.send("sheetMenu", { menu: "timeZone", choose: path });

			await poll(
				`the trigger showing ${shows}`,
				async () => (await control("pane.profile.timeZone")).value === shows,
				5_000,
				100,
			);

			await closeWith("pane.profile.save");
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
		await press("pane.profile.save");
		await Bun.sleep(500);

		const noName = await app.state();

		expect(
			noName.profileSheet === "new" && !newProfile(noName, known),
			`no name saved: ${JSON.stringify(noName.profileSheet)}`,
		);

		const nameRead = await control("pane.profile.name");

		expect(
			nameRead.description === "Profile name is required",
			`no name: ${JSON.stringify(nameRead)}`,
		);

		await render("profile-sheet-name-missing");
		await type("name", "Proxied");

		await poll(
			"the name's problem to go",
			async () => (await control("pane.profile.name")).description === "",
			5_000,
			100,
		);

		// Another platform's warning takes the address past the body's end.
		await press(`pane.profile.platform.${PLATFORMS.other.card}`);
		await toggle("pane.profile.proxy");

		// Where the body ends: the footer's rule, 16 over its buttons.
		const bodyEnd = async () => (await frame("pane.profile.save")).y - 17;

		const hostBefore = await frame("pane.profile.proxyHost");
		const belowTheBody = hostBefore.y + hostBefore.h > (await bodyEnd());

		const proxiedAfresh = async () => {
			await closeWith("pane.profile.cancel");
			await newSheet();
			await type("name", "Proxied");
			await toggle("pane.profile.proxy");
		};

		// Pressed with what cannot save: the sheet stays, nothing is made, and
		// the field comes into view, clear of the rule, its problem's words for
		// the screen reader alone.
		const refuses = async (
			name: string,
			message: string,
			field = "pane.profile.proxyHost",
		) => {
			await press("pane.profile.save");
			// The body's scroll to the field (Motion.reveal, 0.3 s).
			await Bun.sleep(600);
			const state = await app.state();

			expect(
				state.profileSheet === "new" && !newProfile(state, known),
				`${name} saved: ${JSON.stringify(state.profileSheet)}`,
			);

			const shown = await frame("pane.profile.proxyHost");
			const end = await bodyEnd();

			expect(
				shown.y + shown.h <= end - 8,
				`${name}: the host ends at ${shown.y + shown.h}, the body at ${end} (started below it: ${belowTheBody})`,
			);

			const problem = await control(field);

			expect(
				problem.description === message,
				`${name}: ${JSON.stringify(problem)}`,
			);

			await render(`profile-sheet-${name}`, afterScroll);
		};

		await refuses("host-missing", "Proxy host is required");
		await type("proxyHost", "http://127.0.0.1");
		await refuses("host-unreadable", "Host must be a name or an IP address");

		// Afresh: the field holds what was typed, and takes more after it.
		await proxiedAfresh();
		await refuses("host-missing-again", "Proxy host is required");

		// A host, and a port past the last one.
		await type("proxyHost", "127.0.0.1");
		await type("proxyPort", "70000");

		await refuses(
			"port-invalid",
			"Port must be between 1 and 65535",
			"pane.profile.proxyPort",
		);

		// Afresh again, for the proxy string below.
		await proxiedAfresh();
		await refuses("host-missing-once-more", "Proxy host is required");

		await type(
			"proxyHost",
			`127.0.0.1:${proxy.port}:${CREDENTIALS.username}:${CREDENTIALS.password}`,
		);

		// A host now: the problem goes as it is typed.
		await poll(
			"the host's problem to go",
			async () => (await control("pane.profile.proxyHost")).description === "",
			5_000,
			100,
		);

		// Test connection lies past the body's end: a swipe brings it into
		// view, as a hand's does, and takes the body back after, for the render
		// beside the Swift's (whose press needed no view).
		const testButton = await frame("pane.profile.testProxy");
		const body = await frame("pane.profile.body");

		const past = Math.max(
			0,
			Math.ceil(testButton.y + testButton.h - (await bodyEnd()) + 8),
		);

		const swipe = async (dy: number) => {
			await app.send("scrollWindow", {
				x: body.x + body.w / 2,
				y: body.y + body.h / 2,
				dy,
			});

			await Bun.sleep(300);
		};

		if (past > 0) {
			await swipe(-past);
		}

		await press("pane.profile.testProxy");

		const answer = await pollValue(
			async () => (await control("pane.profile.proxyStatus")).name,
			(name) => name === "Connected · 203.0.113.7",
			10_000,
			200,
		);

		expect(
			answer === "Connected · 203.0.113.7",
			`the test's answer: ${JSON.stringify(answer)}`,
		);

		if (past > 0) {
			await swipe(past);
		}

		const fields = await read();

		const filled = {
			host: fields["pane.profile.proxyHost"]?.value === "127.0.0.1",
			port: fields["pane.profile.proxyPort"]?.value === String(proxy.port),
			username:
				fields["pane.profile.proxyUsername"]?.value === CREDENTIALS.username,
		};

		expect(
			Object.values(filled).every(Boolean),
			`the pasted proxy's fields: ${JSON.stringify(filled)}`,
		);

		await render("profile-sheet-proxy", past > 0 ? afterScroll : 300);

		// The form runs past the body here: it ends hard at the rules, which show
		// in both themes, for the eye to check in the window as the screen shows
		// it.
		const nameField = await frame("pane.profile.name");

		await app.send("scrollWindow", {
			x: nameField.x + 40,
			y: nameField.y + 200,
			dy: -12,
		});

		await Bun.sleep(300);

		const window = (await app.state()).nativeWindows.find(
			(each) => each.role === "main",
		)?.number;

		for (const theme of ["light", "dark"]) {
			await app.send("theme", { theme });
			await Bun.sleep(300);

			if (window) {
				await captureWindow(
					window,
					join(context.out, `profile-sheet-scrolled-${theme}.png`),
				);
			}
		}

		await app.send("theme", { theme: "system" });

		await press("pane.profile.save");

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
		// whole row toggles it, as a checkbox's label does. Two reads 100 ms
		// apart agree first.
		let spoofRow = await frame("pane.profile.spoof");

		await poll("the sheet to settle", async () => {
			await Bun.sleep(100);

			const again = await frame("pane.profile.spoof");
			const settled = again.x === spoofRow.x && again.y === spoofRow.y;
			spoofRow = again;

			return settled;
		});

		const spoofBefore = (await control("pane.profile.spoof")).checked;

		await app.send("clickWindow", {
			x: spoofRow.x + 30,
			y: spoofRow.y + spoofRow.h / 2,
		});

		// Until the switch reads its new value, or 3 s pass.
		const spoofAfterClick = await poll(
			"the switch to toggle",
			async () => {
				const { checked } = await control("pane.profile.spoof");

				return checked !== spoofBefore && { checked };
			},
			3_000,
			100,
		)
			.then((read) => read.checked)
			.catch(() => spoofBefore);

		expect(
			spoofBefore === true && spoofAfterClick === false,
			`a click on the label at ${spoofRow.x + 30},${spoofRow.y + spoofRow.h / 2} left the switch at ${spoofAfterClick} (from ${spoofBefore})`,
		);

		// With spoofing off sites see the Mac's zone with the rest of the Mac:
		// once what it hides has gone (Motion.toggle).
		await Bun.sleep(300);

		expect(
			!("pane.profile.timeZone" in (await read())),
			"the time zone's row still shows with spoofing off",
		);

		await closeWith("pane.profile.save");

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
