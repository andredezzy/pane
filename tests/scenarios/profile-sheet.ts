import { readFileSync } from "node:fs";
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
import { type AppState, type PaneApp, tabOf } from "../lib/pane";
import { poll } from "../lib/poll";
import { startHttpProxy } from "../lib/proxies";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// The profile sheet's fingerprint and proxy (apps/veil profile-sheet.tsx),
// filled as a user would: presses and typing through the window's
// accessibility, never the pointer. A profile on the Mac's platform claims
// the Mac's real GPU, the very strings WebGL reports in a profile without a
// fingerprint; a rename keeps the fingerprint; another platform gets that
// platform's preset, the cross-OS note, and reaches the profile's open tab;
// the proxy shortcut fills the fields, its test answers through the proxy,
// its password is kept in the keychain (the mock keychain here), not in
// profiles.json, and turning spoofing off clears the fingerprint.

type Saved = {
	id: string;
	name: string;
	fingerprint: {
		platform: string;
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

async function saved(app: PaneApp, id: string): Promise<Saved | undefined> {
	await app.send("save");

	const file = JSON.parse(
		readFileSync(join(app.dataDir, "profiles.json"), "utf8"),
	) as { state: { profiles: Saved[] } };

	return file.state.profiles.find((profile) => profile.id === id);
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

		const sidebar = await tree("profile-sheet-a11y-sidebar");

		// New profile: spoofing on, the Mac's platform, by default.
		let known = (await app.state()).profiles.map((profile) => profile.id);
		await clickButton(pid, window, "New");

		await app.waitFor(
			"the new profile sheet",
			(state) => state.profileSheet === "new",
		);

		await typeIntoField(pid, window, "pane.profile.name", "Mac claim");

		await app.send("render", {
			path: join(context.out, "profile-sheet-new.png"),
		});

		// The sheet's fields read their labels, not their placeholders, and
		// the sheet is modal: the sidebar under it is out of reach.
		const sheet = await tree("profile-sheet-a11y-sheet");

		const named = {
			plainRow: /AXButton = "1 tab, expanded" \(Plain\)/.test(sidebar),
			name: /AXTextField[^\n]*\(Name\)[^\n]*id=pane\.profile\.name/.test(sheet),
			group: /AXTextField[^\n]*\(Group\)/.test(sheet),
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
			macSaved?.fingerprint?.platform === "MACOS" &&
				macSaved.fingerprint.webgl?.vendor === realWebGL.vendor &&
				macSaved.fingerprint.webgl?.renderer === realWebGL.renderer,
			`the Mac profile's fingerprint: ${JSON.stringify(macSaved?.fingerprint)}`,
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
			renamed?.name === "Mac claim renamed" &&
				JSON.stringify(renamed.fingerprint) ===
					JSON.stringify(macSaved?.fingerprint),
			`the rename changed ${JSON.stringify(renamed)}`,
		);

		// Another platform: its preset, the note, and the open tab follows.
		await app.send("openProfileSheet", { profile: mac });
		await app.waitFor("the edit sheet", (state) => state.profileSheet === mac);
		await clickButton(pid, window, "Windows");
		await Bun.sleep(300);

		await app.send("render", {
			path: join(context.out, "profile-sheet-cross-os.png"),
		});

		await clickButton(pid, window, "Save");

		await app.waitFor(
			"the sheet closed",
			(state) => state.profileSheet === null,
		);

		const windows = await saved(app, mac);

		expect(
			windows?.fingerprint?.platform === "WINDOWS" &&
				windows.fingerprint.webgl?.vendor === "Google Inc. (NVIDIA)",
			`the Windows fingerprint: ${JSON.stringify(windows?.fingerprint)}`,
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

		// The proxy: the shortcut, its test, and Create.
		known = (await app.state()).profiles.map((profile) => profile.id);
		await clickButton(pid, window, "New");

		await app.waitFor(
			"the new profile sheet",
			(state) => state.profileSheet === "new",
		);

		await typeIntoField(pid, window, "pane.profile.name", "Proxied");

		await clickControl(pid, window, {
			title: "Enable proxy",
			role: /AX(CheckBox|Switch)/,
		});

		await typeIntoField(
			pid,
			window,
			"pane.profile.proxyShortcut",
			`127.0.0.1:${proxy.port}:${CREDENTIALS.username}:${CREDENTIALS.password}`,
		);

		await clickButton(pid, window, "Test proxy");
		await clickControl(pid, window, { title: "203.0.113.7" }).catch(() => {});

		await app.send("render", {
			path: join(context.out, "profile-sheet-proxy.png"),
		});

		await clickButton(pid, window, "Create");

		const proxied = await app.waitFor(
			"the proxied profile",
			(state) => !!newProfile(state, known) && state.profileSheet === null,
		);

		const proxiedID = newProfile(proxied, known) ?? "";
		const proxiedSaved = await saved(app, proxiedID);

		const savedProxy = proxiedSaved?.proxy;

		const keychain = JSON.parse(
			readFileSync(join(app.dataDir, "mock-keychain.json"), "utf8"),
		) as { state: Record<string, string> };

		expect(
			savedProxy?.proxyType === "HTTP" &&
				savedProxy.host === "127.0.0.1" &&
				savedProxy.port === proxy.port &&
				savedProxy.username === CREDENTIALS.username &&
				savedProxy.password === null &&
				keychain.state[proxiedID] === CREDENTIALS.password,
			`the saved proxy: ${JSON.stringify({ ...savedProxy, password: savedProxy?.password ? "(set)" : null, inKeychain: proxiedID in keychain.state })}`,
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
		// whole row toggles it, as a checkbox's label does.
		// Read once the sheet has slid in: two reads 100 ms apart agree.
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
			`a click on the label left the switch at ${spoofAfterClick} (from ${spoofRow.value})`,
		);

		await clickButton(pid, window, "Save");

		await app.waitFor(
			"the sheet closed",
			(state) => state.profileSheet === null,
		);

		const unspoofed = await saved(app, proxiedID);

		expect(
			unspoofed?.fingerprint === null,
			`spoofing off kept ${JSON.stringify(unspoofed?.fingerprint)}`,
		);

		return {
			hostWebGL: host,
			webGLInAPlainProfile: realWebGL,
			macProfile: macSaved?.fingerprint,
			accessibilityNames: named,
			renameKeptFingerprint: true,
			crossOS: {
				fingerprint: windows?.fingerprint,
				openTab: { before, after },
			},
			proxy: {
				saved: proxiedSaved?.proxy,
				passwordInKeychain: true,
				testedThrough: proxy.log.length,
			},
			spoofingOff: {
				byLabelClick: spoofAfterClick,
				saved: unspoofed?.fingerprint,
			},
		};
	} finally {
		proxy.stop();
	}
};

export default profileSheet;
