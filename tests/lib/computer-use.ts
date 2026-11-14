// Pane's computer use (ADR 0012): Aside Computer Use, the driver a person's
// checks go through on the Mac, for what only real input proves: a press on a
// control, a key, the front. A menu bar item is chosen through accessibility
// instead (invokeMenu), with no menu shown. What a check reads of a window
// comes from the runner's own tools, which need no driver: the window list
// and the accessibility tree (windows.ts), and the capture (tools.ts).
// Windows' tree is UI Automation's, which W8 checks with Narrator (ADR 0071),
// so there the UI is never driven (canDriveUI) and a call throws.
//
// One server per runner (`aside mcp --computer-use`, which keeps a task on
// Aside's list), allowed Pane's identifier alone, its repl driven over stdio.
// A test copy shares the user's Pane's identifier and name, so it is asked
// for by its bundle's path, which Aside binds to that copy alone (a name two
// running apps share it refuses), and only while its process runs: Aside
// opens an app it is asked for that isn't running, and LaunchServices may
// then open another Pane. Before every action the windows Aside holds are
// proved the copy's (AGENTS.md); the state Aside prints is never written out.
import type { Subprocess } from "bun";
import { bySystem, noWindowsSide } from "./platform";
import { swiftTool } from "./tools";
import { ownedWindowIds, windowsOf } from "./windows";

const PANE = "xyz.roundedcorners.pane";

// macOS hides every window from accessibility and stops delivering input while
// the screen is locked, so visual checks must wait for an unlocked session.
// Windows shows its lock and sign-in screens through LogonUI.exe, which runs
// only while one shows, and Chromium counts its windows occluded meanwhile.
export const isScreenLocked = bySystem({
	mac: async () => {
		const output = await Bun.$`ioreg -n Root -d1 -a`.quiet().text();

		return /<key>CGSSessionScreenIsLocked<\/key>\s*<true\/>/.test(output);
	},
	windows: async () => {
		const list = Bun.spawn(
			["tasklist", "/FI", "IMAGENAME eq LogonUI.exe", "/FO", "CSV", "/NH"],
			{ stdout: "pipe", stderr: "ignore" },
		);

		return (await new Response(list.stdout).text()).includes('"LogonUI.exe"');
	},
});

// Whether checks of the native UI can run: the screen unlocked, and Aside's
// command installed (GitHub's runners have none). Computer Use turned off in
// Aside says so at the first action. Never on Windows, whose tree this file
// doesn't read.
export const canDriveUI = bySystem({
	mac: async () => Bun.which("aside") !== null && !(await isScreenLocked()),
	windows: async () => false,
});

// The app's largest window. A sheet is a window of its own, and the windows
// come in no fixed order; only the main window holds the sheet's controls.
export async function mainWindowOf(pid: number): Promise<number> {
	const { windows } = await windowsOf(pid);

	const area = ({ bounds }: (typeof windows)[number]) =>
		(bounds.Width ?? 0) * (bounds.Height ?? 0);

	const [main] = windows.sort((a, b) => area(b) - area(a));

	if (main === undefined) {
		throw new Error(`no main window for pid ${pid}`);
	}

	return main.id;
}

type Reply = { text: string; error: boolean };

// The MCP server, one request at a time.
class Driver {
	static #one: Promise<Driver> | undefined;

	readonly #server: Subprocess<"pipe", "pipe", "ignore">;
	readonly #output: ReadableStreamDefaultReader<Uint8Array>;
	readonly #decoder = new TextDecoder();
	#buffer = "";
	#next = 0;
	#turn: Promise<unknown> = Promise.resolve();

	private constructor(server: Subprocess<"pipe", "pipe", "ignore">) {
		this.#server = server;
		this.#output = server.stdout.getReader();
	}

	static get(): Promise<Driver> {
		Driver.#one ??= Driver.#start();

		return Driver.#one;
	}

	static async #start(): Promise<Driver> {
		const server = Bun.spawn(
			["aside", "mcp", "--computer-use", "--app", PANE],
			{ stdin: "pipe", stdout: "pipe", stderr: "ignore" },
		);

		// It ends with the runner, whose exit closes its input.
		server.unref();

		const driver = new Driver(server);

		await driver.#request("initialize", {
			protocolVersion: "2025-06-18",
			capabilities: {},
			clientInfo: { name: "pane-scenarios", version: "1" },
		});

		driver.#send({ jsonrpc: "2.0", method: "notifications/initialized" });

		return driver;
	}

	#send(message: object): void {
		this.#server.stdin.write(`${JSON.stringify(message)}\n`);
		this.#server.stdin.flush();
	}

	async #request(
		method: string,
		params: object,
	): Promise<Record<string, unknown>> {
		const id = ++this.#next;
		this.#send({ jsonrpc: "2.0", id, method, params });

		for (;;) {
			const newline = this.#buffer.indexOf("\n");

			if (newline === -1) {
				const { value, done } = await this.#output.read();

				if (done) {
					throw new Error("Aside's computer use ended");
				}

				this.#buffer += this.#decoder.decode(value, { stream: true });

				continue;
			}

			const line = this.#buffer.slice(0, newline);
			this.#buffer = this.#buffer.slice(newline + 1);

			let message: {
				id?: number;
				result?: Record<string, unknown>;
				error?: { message?: string };
			};

			try {
				message = JSON.parse(line);
			} catch {
				continue;
			}

			if (message.id !== id) {
				continue;
			}

			if (message.error) {
				throw new Error(`Aside's computer use: ${message.error.message}`);
			}

			return message.result ?? {};
		}
	}

	// Runs `code` in the repl, whose variables persist between cells.
	run(code: string, title: string): Promise<Reply> {
		const turn = this.#turn.then(async () => {
			const result = await this.#request("tools/call", {
				name: "repl",
				arguments: { code, title },
			});

			const content = (result.content ?? []) as { text?: string }[];

			return {
				text: content.map((item) => item.text ?? "").join("\n"),
				error: result.isError === true,
			};
		});

		this.#turn = turn.catch(() => undefined);

		return turn;
	}
}

// The copy's bundle, from its process while it runs: the path Aside binds.
async function bundleOf(pid: number): Promise<string> {
	const executable = (
		await Bun.$`ps -o comm= -p ${pid}`.quiet().nothrow().text()
	).trim();

	const bundle = executable.match(/^(.*?\.app)\/Contents\/MacOS\/[^/]+$/)?.[1];

	if (!bundle) {
		throw new Error(`pid ${pid} is no running app to drive`);
	}

	return bundle;
}

type Done = Record<string, unknown>;

// Runs `action`, the body of an async function given `pane` (the copy's
// handle) and `values`, once every window Aside holds of the app is proved
// the copy's; what it returns comes back. A window that opened since the copy's
// were listed refuses once, and they are listed again.
async function drive(
	pid: number,
	title: string,
	action: string,
	values: Record<string, unknown> = {},
): Promise<Done> {
	return bySystem({
		mac: async (): Promise<Done> => {
			const driver = await Driver.get();

			for (let attempt = 1; ; attempt++) {
				// Asked for only while it runs, its path read from its process.
				const bundle = await bundleOf(pid);
				const own = await ownedWindowIds(pid);

				const code = `await (async () => {
					const values = ${JSON.stringify({ ...values, bundle, own, pid })};
					globalThis.panes ??= {};
					panes[values.pid] ??= await computer.getApp({ name: values.bundle, access: "control" });
					const pane = panes[values.pid];
					const held = (await pane.listWindows()).map((window) => Number(String(window.windowId).replace(/^w/, "")));
					if (held.length === 0 || !held.every((id) => values.own.includes(id))) {
						console.log("@@" + JSON.stringify({ refused: held.length }));
						return;
					}
					const done = await (async () => { ${action} })();
					console.log("@@" + JSON.stringify(done ?? {}));
				})();`;

				const reply = await driver.run(code, `Pane scenario: ${title}`);

				const marker = reply.text
					.split("\n")
					.reverse()
					.find((line) => line.startsWith("@@"));

				if (marker === undefined) {
					// The copy's state alone can follow an error, as the proof ran
					// before any action: only the error's own line is quoted.
					const why = reply.text
						.split("\n")
						.find((line) => /error/i.test(line))
						?.slice(0, 300);

					throw new Error(`computer use: ${title}: ${why ?? "no result"}`);
				}

				const done = JSON.parse(marker.slice(2)) as Done;

				if (done.refused === undefined) {
					return done;
				}

				if (attempt >= 2) {
					throw new Error(
						`computer use: ${title}: Aside held windows that aren't the test copy's, so nothing was done`,
					);
				}
			}
		},
		windows: async (): Promise<Done> => {
			throw noWindowsSide(
				`computer use's ${title}`,
				"W8",
				"Windows' tree is UI Automation's, checked with Narrator (ADR 0071)",
			);
		},
	})();
}

// A row of Aside's state: `[N] role label`, the label perhaps followed by
// `, ID: <identifier>` and the element's secondary actions.
const ROW = String.raw`^\s*\[(\d+)\] (?:(?:[a-z]+ )*?)`;

// Clicks the control titled `title` (a button, a link, or a menu item; `id`
// picks one of several by its identifier) in the window, or in whichever of
// the app's windows has it: a sheet is a window of its own. The control is
// looked for until it shows, up to 5 seconds, as a sheet is in the app's state
// a moment before its controls reach accessibility.
export async function clickButton(
	pid: number,
	window: number | "any",
	title: string,
	id?: string,
): Promise<void> {
	const done = await drive(
		pid,
		`press "${title}"`,
		`
		const escape = (text) => text.replace(/[.*+?^\${}()|[\\]\\\\]/g, "\\\\$&");
		const row = new RegExp(${JSON.stringify(ROW)} + "(?:button|link|menu item) " + escape(values.title) + "(?:,|$| )");
		const until = Date.now() + 5000;
		while (Date.now() < until) {
			const windows = values.window === "any"
				? (await pane.listWindows()).map((listed) => listed.windowId)
				: ["w" + values.window];
			for (const windowId of windows) {
				const state = String(await pane.getAXState({ query: values.title, windowId, disableDiffing: true, emit: false }));
				const line = state.split("\\n").find((text) => row.test(text) && (!values.id || text.includes("ID: " + values.id)));
				if (line) {
					await pane.click(Number(line.match(/\\[(\\d+)\\]/)[1]));
					return { clicked: true };
				}
			}
			await sleep(200);
		}
		return { clicked: false };
		`,
		{ window, title, id },
	);

	if (!done.clicked) {
		throw new Error(`no control "${title}" to press`);
	}
}

// Chooses an item of the app's menu bar through accessibility, as choosing it
// does, `path` from the menu's title down (tests/tools/ax-menu.swift): the
// item is pressed with no menu shown, so a copy behind the user's apps needs
// no front and shows nothing over them. Aside opens a menu bar item's menu to
// choose from it, which the background can't allow.
export const invokeMenu = bySystem({
	mac: async (pid: number, path: string[]): Promise<void> => {
		const pressed = await Bun.$`${await swiftTool("ax-menu")} ${pid} ${path}`
			.quiet()
			.nothrow();

		if (pressed.exitCode !== 0) {
			throw new Error(
				`${path.join(" › ")}: ${pressed.stderr.toString().trim()}`,
			);
		}
	},
	windows: async (_pid: number, path: string[]): Promise<void> => {
		throw noWindowsSide(
			`the menu bar's ${path.join(" › ")}`,
			"W5",
			"where Windows' menu bar commands live is drawn with the rest of its look",
		);
	},
});

// A key or a chord, as X names its keys: `Escape`, `Return`, `Down`, `space`,
// `Super_L+t` (⌘T), `Shift_L+Tab`. The names a step writes in lower case
// (`escape`, `down`, `cmd+shift+t`) are taken too.
function keysym(chord: string): string {
	const names: Record<string, string> = {
		cmd: "Super_L",
		command: "Super_L",
		shift: "Shift_L",
		option: "Alt_L",
		alt: "Alt_L",
		ctrl: "Control_L",
		control: "Control_L",
		escape: "Escape",
		esc: "Escape",
		return: "Return",
		enter: "Return",
		tab: "Tab",
		space: "space",
		delete: "BackSpace",
		backspace: "BackSpace",
		up: "Up",
		down: "Down",
		left: "Left",
		right: "Right",
		home: "Home",
		end: "End",
		pageup: "Page_Up",
		pagedown: "Page_Down",
		plus: "plus",
	};

	return chord
		.split("+")
		.map((key) => names[key.toLowerCase()] ?? key)
		.join("+");
}

// Presses a key or a chord in the copy (keysym).
export async function pressKey(pid: number, chord: string): Promise<void> {
	await drive(pid, `press ${chord}`, "await pane.pressKey(values.key);", {
		key: keysym(chord),
	});
}

// Types `text` into the copy's focused field, as keys, 256 at most per call.
export async function typeText(pid: number, text: string): Promise<void> {
	await drive(pid, "type", "await pane.typeText(values.text);", { text });
}

// A click, or a drag, at points in the copy's main window, from its top left,
// as the window's own capture shows them at 1×: Aside takes the window's
// screenshot first, whose points its clicks follow.
export async function clickAt(
	pid: number,
	x: number,
	y: number,
	button: "left" | "right" = "left",
): Promise<void> {
	await drive(
		pid,
		`${button} click`,
		`
		await pane.getScreenshot();
		await pane.click([values.x, values.y], { mouseButton: values.button });
		`,
		{ x, y, button },
	);
}

export async function dragFrom(
	pid: number,
	from: [number, number],
	to: [number, number],
): Promise<void> {
	await drive(
		pid,
		"drag",
		`
		await pane.getScreenshot();
		await pane.drag(values.from, values.to);
		`,
		{ from, to },
	);
}

// The copy in front with real input, the user's mouse and keyboard held by
// Aside until `leaveFront`: only on the rules for taking the front
// (docs/building.md), for a step the background can't do.
export async function takeFront(pid: number, why: string): Promise<void> {
	await drive(
		pid,
		"take the front",
		"await pane.requestForeground(values.why);",
		{
			why,
		},
	);
}

export async function leaveFront(pid: number): Promise<void> {
	await drive(pid, "leave the front", "await pane.endForeground();");
}
