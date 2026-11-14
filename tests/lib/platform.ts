// The one place the scenario runner tests for the OS, ADR 0124's rule in the
// runner's form: a module that does something the Mac's way and Windows' way
// holds both sides and picks one through `bySystem`, and no other file reads
// process.platform. A scenario never calls a system's tool itself, but one
// the runner keeps off Windows (`NotOnWindows`), whose subject is the Mac's,
// as `pane/model/mac` is the Mac's alone. What Windows has no side for yet
// throws (`noWindowsSide`), so nothing there passes for working.

type System = "mac" | "windows";

function thisSystem(): System {
	if (process.platform === "darwin") {
		return "mac";
	}

	if (process.platform === "win32") {
		return "windows";
	}

	throw new Error(
		`the scenarios run on the Mac and Windows, not ${process.platform}`,
	);
}

const system = thisSystem();

// This system's side of something done one way on the Mac and another on
// Windows.
export function bySystem<T>(sides: Record<System, T>): T {
	return sides[system];
}

// What a Windows side throws where it has none yet: `owner` is the item of
// docs/plans/views-migration.md that brings it (W3 Windows' data, W4 its
// platform files, W7 its install and update, W8 its audits) or the user's
// decision it rests on (U5, U6), and `why` says what it waits on.
export function noWindowsSide(
	what: string,
	owner: string,
	why?: string,
): Error {
	return new Error(
		`${what}: no Windows side yet (${owner}, docs/plans/views-migration.md)${why ? `; ${why}` : ""}`,
	);
}

// The engine's folder in Pane's data folder, Chromium's user data dir, which
// holds its Local State, its Default profile, and each profile's folder by
// its id (pane/model/engine_folder.h): Engine on the Mac; on Windows User
// Data, the folder chrome_elf names for a launch with no --user-data-dir, as
// the user's is, and which a launch's must be.
export const ENGINE_FOLDER = bySystem({ mac: "Engine", windows: "User Data" });

// The modifier of Pane's shortcuts, by the channel's name for it: Command on
// the Mac, Control on Windows, where Pane's command table gives its keys
// Ctrl (pane/ui/app/commands.cc).
export const PRIMARY = bySystem({
	mac: "command",
	windows: "control",
} as const);

// A key as the channel takes it.
export type Shortcut = { key: string; modifiers: string[] };

// Pane's keys that differ by more than PRIMARY, as the command table gives
// each system its own (Chrome's own Windows keys there): Back and Forward,
// Developer Tools' second key, Chrome's (⌥⌘I on the Mac, F12 on Windows), and
// Full Screen (⌃⌘F, F11).
export const KEYS = bySystem<
	Record<"back" | "forward" | "devToolsAgain" | "fullScreen", Shortcut>
>({
	mac: {
		back: { key: "[", modifiers: ["command"] },
		forward: { key: "]", modifiers: ["command"] },
		devToolsAgain: { key: "i", modifiers: ["command", "option"] },
		fullScreen: { key: "f", modifiers: ["command", "control"] },
	},
	windows: {
		back: { key: "left", modifiers: ["option"] },
		forward: { key: "right", modifiers: ["option"] },
		devToolsAgain: { key: "f12", modifiers: [] },
		fullScreen: { key: "f11", modifiers: [] },
	},
});

// A scenario's own word that it doesn't run on Windows (`export const
// notOnWindows`), which the runner reads; without it, a scenario runs on both.
export type NotOnWindows =
	// Its subject is the Mac's alone, and Windows has nothing of it: the
	// runner leaves it out there, a full run all the same.
	| { macOnly: string }
	// Windows does it its own way, not designed yet: the runner leaves it
	// out there as checks not made, so the run isn't complete.
	| { needsDesign: string };

// What keeps a scenario off this system, by its declaration: nothing on the
// Mac, where every scenario runs.
export const keptOffThisSystem = bySystem({
	mac: (_declared: NotOnWindows | undefined): NotOnWindows | undefined =>
		undefined,
	windows: (declared: NotOnWindows | undefined) => declared,
});

// What a PowerShell script prints, for a Windows side, with `env` added to
// the process's: paths go in through `env` ($env:NAME), never into the
// script's text, so nothing in one is read as PowerShell. The script goes
// encoded, past the command line's quoting.
export async function powershell(
	script: string,
	env: Record<string, string> = {},
): Promise<string> {
	const child = Bun.spawn(
		[
			"powershell.exe",
			"-NoProfile",
			"-NonInteractive",
			"-EncodedCommand",
			Buffer.from(script, "utf16le").toString("base64"),
		],
		{
			env: { ...process.env, ...env },
			stdin: "ignore",
			stdout: "pipe",
			stderr: "pipe",
		},
	);

	const [output, error, code] = await Promise.all([
		new Response(child.stdout).text(),
		new Response(child.stderr).text(),
		child.exited,
	]);

	if (code !== 0) {
		throw new Error(`PowerShell exited with ${code}: ${error.trim()}`);
	}

	return output;
}

// The same, waited for where the caller can't wait (a key derived for a
// check, tests/lib/engine-key.ts).
export function powershellSync(
	script: string,
	env: Record<string, string> = {},
): string {
	const child = Bun.spawnSync(
		[
			"powershell.exe",
			"-NoProfile",
			"-NonInteractive",
			"-EncodedCommand",
			Buffer.from(script, "utf16le").toString("base64"),
		],
		{ env: { ...process.env, ...env }, stdin: "ignore" },
	);

	if (child.exitCode !== 0) {
		throw new Error(
			`PowerShell exited with ${child.exitCode}: ${child.stderr.toString().trim()}`,
		);
	}

	return child.stdout.toString();
}
