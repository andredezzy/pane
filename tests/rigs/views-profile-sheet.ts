import { launchPane } from "../lib/pane";
import { poll, pollValue } from "../lib/poll";
import { startSocks5Proxy } from "../lib/proxies";
import { expect, type Scenario } from "../run";

// The profile sheet's states that the Swift's renders (profile-sheet) don't
// hold, drawn in both themes at 2× beside Paper's 05.5 and 05.6: a profile
// edited with its history turned off, Windows claimed and a zone of its own,
// behind a SOCKS5 proxy with a username, its test answered through the run's
// own proxy (Connected), then refused (a closed port), then waiting on a
// proxy that never answers (Testing…) until it times out; spoofing off; Linux
// chosen. Driven as a hand does: real clicks on the sheet's controls, text
// set as a screen reader sets it, and the zone and the proxy's type chosen in
// their menus' models (the system's menus need the front).

const CREDENTIALS = { username: "travel-user", password: "proxy-secret" };

type Item = { title: string; checked?: boolean; submenu?: Item[] };

const viewsProfileSheet: Scenario = async (context) => {
	const fixturePort = Number(new URL(context.fixtures.origin).port);
	const socks = await startSocks5Proxy(fixturePort, CREDENTIALS);

	try {
		const app = await launchPane(context, {
			env: { PANE_PROXY_TEST_URL: "http://check.pane.test/ip" },
		});

		const frame = async (name: string) => {
			const [x = 0, y = 0, w = 0, h = 0] = (await app.frames(name))[name] ?? [];

			return { x, y, w, h };
		};

		const control = async (name: string) =>
			(
				await app.send<Record<string, { name: string; value: string }>>(
					"accessibility",
					{ prefix: name },
				)
			).result[name];

		const type = (field: string, text: string) =>
			app.send("sheetField", { field, text });

		// A swipe on the body: negative moves its content up.
		const swipeBy = async (dy: number, settleMs = 300) => {
			const body = await frame("pane.profile.body");

			await app.send("scrollWindow", {
				x: body.x + body.w / 2,
				y: body.y + body.h / 2,
				dy,
			});

			await Bun.sleep(settleMs);
		};

		// The body swiped to its top or its end, as a hand does, and left until
		// Views' overlay scroller has gone (0.5 s after the scroll).
		const swipe = (dy: number) => swipeBy(dy, 1_200);

		// A control of the body brought into view first, as a hand swipes to
		// what it reaches for; the header's and footer's are always in view.
		const press = async (name: string) => {
			if (name.startsWith("pane.profile.") && !/cancel|save|close/.test(name)) {
				const body = await frame("pane.profile.body");
				const target = await frame(name);
				const below = target.y + target.h - (body.y + body.h) + 12;
				const above = body.y - target.y + 12;

				if (below > 12) {
					await swipeBy(-below);
				} else if (above > 12) {
					await swipeBy(above);
				}
			}

			await app.click(name);
			await Bun.sleep(400);
		};

		// The pointer on the sheet's title, where nothing answers a hover.
		const render = async (name: string) => {
			const sheet = await frame("pane.profileSheet");
			await app.send("hoverWindow", { x: sheet.x + 32, y: sheet.y + 28 });

			for (const theme of ["light", "dark"]) {
				await app.send("theme", { theme });
				await app.render(`profile-sheet-${name}-${theme}.png`, 400);
			}

			await app.send("theme", { theme: "system" });
		};

		const testAnswer = async (expected: (name: string) => boolean) => {
			await press("pane.profile.testProxy");

			return pollValue(
				async () => (await control("pane.profile.proxyStatus"))?.name ?? "",
				expected,
				15_000,
				200,
			);
		};

		// A new profile, filled: Windows, New York, a SOCKS5 proxy with its
		// credentials.
		await app.click("pane.sidebar.new");

		await app.waitFor(
			"the new profile sheet",
			(state) =>
				state.profileSheet === "new" && state.focus === "pane.profile.name",
		);

		await Bun.sleep(300);
		await type("name", "Travel");
		await press("pane.profile.platform.windows");

		const zones = await poll(
			"the zones listed",
			async () => {
				const items = (
					await app.send<Item[]>("sheetMenu", { menu: "timeZone" })
				).result;

				return items.some((item) => item.title === "America") && items;
			},
			10_000,
			100,
		);

		const newYork = zones
			.find((item) => item.title === "America")
			?.submenu?.find((item) => item.title.startsWith("New York"))?.title;

		expect(newYork !== undefined, `no New York: ${JSON.stringify(zones)}`);

		await app.send("sheetMenu", {
			menu: "timeZone",
			choose: ["America", newYork],
		});

		await press("pane.profile.proxy");
		await app.send("sheetMenu", { menu: "proxyType", choose: ["SOCKS5"] });
		await type("proxyHost", "127.0.0.1");
		await type("proxyPort", String(socks.port));
		await type("proxyUsername", CREDENTIALS.username);
		await type("proxyPassword", CREDENTIALS.password);

		const known = (await app.state()).profiles.map((profile) => profile.id);
		await press("pane.profile.save");

		const state = await app.waitFor(
			"the profile made",
			(each) =>
				each.profileSheet === null &&
				each.profiles.some((profile) => !known.includes(profile.id)),
		);

		const travel = state.profiles.find(
			(profile) => !known.includes(profile.id),
		)?.id;

		expect(travel !== undefined, "no profile made");

		// Edited: its history turned off (Saving clears what it kept), and its
		// proxy tested (Paper 05.5).
		await Bun.sleep(300);
		await app.send("openProfileSheet", { profile: travel });

		await app.waitFor(
			"the edit sheet",
			(each) =>
				each.profileSheet === travel && each.focus === "pane.profile.name",
		);

		await Bun.sleep(300);
		await press("pane.profile.history");
		await render("edit-history-off");

		await swipe(-2_000);

		const connected = await testAnswer((name) => name.startsWith("Connected"));

		expect(
			connected === "Connected · 203.0.113.7",
			`the test's answer: ${JSON.stringify(connected)}`,
		);

		await render("edit-connected");

		// A port nothing listens on: refused (Paper 05.6's answer).
		await type("proxyPort", "9");
		await Bun.sleep(300);

		const refused = await testAnswer(
			(name) => name !== "" && !name.startsWith("Testing"),
		);

		expect(
			refused === "Connection refused",
			`the refused test's answer: ${JSON.stringify(refused)}`,
		);

		await render("edit-refused");

		// A proxy that takes the connection and never answers: the test shows
		// its yellow dot while it waits, then times out on red.
		const silent = Bun.listen({
			hostname: "127.0.0.1",
			port: 0,
			socket: { data() {} },
		});

		try {
			await type("proxyPort", String(silent.port));
			await Bun.sleep(300);
			await press("pane.profile.testProxy");

			const testing = await pollValue(
				async () => (await control("pane.profile.proxyStatus"))?.name ?? "",
				(name) => name === "Testing…",
				5_000,
				50,
			);

			expect(testing === "Testing…", `the test running: ${testing}`);
			await render("edit-testing");

			const timedOut = await pollValue(
				async () => (await control("pane.profile.proxyStatus"))?.name ?? "",
				(name) => name !== "Testing…",
				20_000,
				200,
			);

			expect(
				timedOut === "Connection timed out",
				`the silent proxy's answer: ${JSON.stringify(timedOut)}`,
			);

			await render("edit-timed-out");
		} finally {
			silent.stop(true);
		}

		// Spoofing off (05.6), then on again with Linux chosen.
		await swipe(2_000);
		await press("pane.profile.spoof");
		await render("edit-spoofing-off");
		await press("pane.profile.spoof");
		await press("pane.profile.platform.linux");
		await render("edit-linux");

		await press("pane.profile.cancel");

		await app.waitFor("the sheet closed", (each) => each.profileSheet === null);

		await app.quit();
	} finally {
		socks.stop();
	}
};

export default viewsProfileSheet;
