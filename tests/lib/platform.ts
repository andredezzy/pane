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

// What a Windows side throws where it has none yet: the second part of W6
// (docs/plans/views-migration.md) brings the channel's Windows side and the
// runner's tools there; `why` says what else it waits on.
export function noWindowsSide(what: string, why?: string): Error {
	return new Error(
		`${what}: no Windows side yet (W6, docs/plans/views-migration.md)${why ? `; ${why}` : ""}`,
	);
}

// The modifier of Pane's shortcuts, by the channel's name for it: Command on
// the Mac, Control on Windows, where Pane's command table gives its keys
// Ctrl (W4).
export const PRIMARY = bySystem({
	mac: "command",
	windows: "control",
} as const);

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
