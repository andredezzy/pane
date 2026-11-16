import { createHash, randomBytes } from "node:crypto";
import {
	closeSync,
	cpSync,
	existsSync,
	mkdirSync,
	openSync,
	readdirSync,
	readFileSync,
	statSync,
	writeFileSync,
	writeSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, relative, sep } from "node:path";
import type { ScenarioContext } from "../run";
import { appOf } from "./browser";
import { launchPane } from "./pane";
import { bySystem } from "./platform";
import { pollValue } from "./poll";
import { pidsMatching } from "./processes";
import {
	compileC,
	copyBundle,
	makeDmg,
	setPlistString,
	signAdHoc,
} from "./tools";

// What the update scenarios install from and into (ADR 0092, ADR 0127): a
// stand-in release's file, holding a stand-in Pane, and a copy of the run's
// Pane in a folder of its own, since an update replaces the app it runs from.
// The Mac's: a dmg holding a tiny app with Pane's identifier that writes
// `reopened` into the data folder when it opens, and a copy of the bundle.
// Windows': Pane's own installer, its payload the run's app folder with
// another pane-version, and a copy of the app folder laid out as an install
// lays it out (Pane\Application, its update folder beside it).

export type StandInDmg = {
	name: string;
	version: string;
	path: string;
	sha256: string;
};

// Whether the swap runs once Pane has exited, in a helper, where a file held
// open in the app folder can fail it and roll it back: Windows' (ADR 0127).
// The Mac swaps its bundles in one rename before it quits, which a file held
// open doesn't stop.
export const SWAP_AFTER_EXIT = bySystem({ mac: false, windows: true });

// A dmg offered as `offered`, holding a stand-in Pane that says `holds`, with
// `padding` bytes of noise in it, so a slow server's download lasts.
export async function standInDmg(
	work: string,
	holds: string,
	offered: string,
	padding = 0,
): Promise<StandInDmg> {
	const root = join(work, `dmg-${offered}`);
	const contents = join(root, "Pane.app", "Contents");
	mkdirSync(join(contents, "MacOS"), { recursive: true });

	if (padding > 0) {
		mkdirSync(join(contents, "Resources"), { recursive: true });
		writeFileSync(join(contents, "Resources", "padding"), randomBytes(padding));
	}

	const keys = {
		CFBundleExecutable: "Pane",
		CFBundleIdentifier: "xyz.roundedcorners.pane",
		CFBundlePackageType: "APPL",
		CFBundleShortVersionString: holds,
		PaneVersion: holds,
	};

	const entries = Object.entries(keys).map(
		([key, value]) => `<key>${key}</key><string>${value}</string>`,
	);

	writeFileSync(
		join(contents, "Info.plist"),
		`<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd"><plist version="1.0"><dict>${entries.join("")}</dict></plist>`,
	);

	const source = join(work, "reopened.c");

	writeFileSync(
		source,
		'#include <stdio.h>\n#include <stdlib.h>\nint main(void) { const char *d = getenv("PANE_DATA_DIR"); char p[4096]; FILE *f; if (!d) return 1; snprintf(p, sizeof p, "%s/reopened", d); f = fopen(p, "w"); if (!f) return 1; fputs(VERSION, f); fclose(f); return 0; }\n',
	);

	await compileC(source, join(contents, "MacOS", "Pane"), [
		`VERSION="${holds}"`,
	]);

	await signAdHoc(join(root, "Pane.app"));

	const name = `Pane-${offered}-arm64.dmg`;
	const path = join(work, name);
	await makeDmg(root, path, "Pane");

	return {
		name,
		version: offered,
		path,
		sha256: createHash("sha256").update(readFileSync(path)).digest("hex"),
	};
}

// 7-Zip's variable-length number (7zFormat.txt's NUMBER).
function sevenZipNumber(out: number[], value: number) {
	let first = 0;
	let mask = 0x80;
	let extra = 0;

	for (; extra < 8; extra++) {
		if (value < 2 ** (7 * (extra + 1))) {
			first |= Math.floor(value / 2 ** (8 * extra));

			break;
		}

		first |= mask;
		mask >>= 1;
	}

	out.push(first & 0xff);

	for (let byte = 0, rest = value; byte < extra; byte++) {
		out.push(rest % 256);
		rest = Math.floor(rest / 256);
	}
}

// Bits as 7-Zip packs them, the first item in the first byte's high bit.
function sevenZipBits(bits: boolean[]): number[] {
	const bytes = new Array<number>(Math.ceil(bits.length / 8)).fill(0);

	bits.forEach((bit, index) => {
		if (bit) {
			bytes[index >> 3] = (bytes[index >> 3] ?? 0) | (0x80 >> (index & 7));
		}
	});

	return bytes;
}

type Stored = { name: string; size: number; data: () => Uint8Array };

// A 7z archive's header for `entries` stored as they are, each its own
// folder of 7-Zip's Copy method, so the installer's reader writes each
// straight into its file, as it does each of a release's LZMA folders; no
// CRCs, which 7z leaves optional and the payload's SHA-256 covers.
function sevenZipHeader(entries: Stored[]): Uint8Array {
	const streams = entries.filter((entry) => entry.size > 0);
	const out: number[] = [0x01, 0x04, 0x06];
	sevenZipNumber(out, 0);
	sevenZipNumber(out, streams.length);
	out.push(0x09);

	for (const stream of streams) {
		sevenZipNumber(out, stream.size);
	}

	out.push(0x00, 0x07, 0x0b);
	sevenZipNumber(out, streams.length);
	out.push(0x00);

	for (const _ of streams) {
		// One coder, a one-byte id, simple: Copy (0x00).
		out.push(0x01, 0x01, 0x00);
	}

	out.push(0x0c);

	for (const stream of streams) {
		sevenZipNumber(out, stream.size);
	}

	out.push(0x00, 0x00, 0x05);
	sevenZipNumber(out, entries.length);
	const empty = entries.map((entry) => entry.size === 0);

	if (empty.some(Boolean)) {
		const streamBits = sevenZipBits(empty);
		out.push(0x0e);
		sevenZipNumber(out, streamBits.length);
		out.push(...streamBits);
		// Each empty one a file, not a folder.
		const fileBits = sevenZipBits(empty.filter(Boolean));
		out.push(0x0f);
		sevenZipNumber(out, fileBits.length);
		out.push(...fileBits);
	}

	const names = Buffer.concat(
		entries.map((entry) => Buffer.from(`${entry.name}\0`, "utf16le")),
	);

	out.push(0x11);
	sevenZipNumber(out, names.length + 1);
	out.push(0x00, ...names, 0x00, 0x00);

	return Uint8Array.from(out);
}

function littleEndian(value: number, bytes: number): Buffer {
	const buffer = Buffer.alloc(bytes);

	if (bytes === 8) {
		buffer.writeBigUInt64LE(BigInt(value));
	} else {
		buffer.writeUInt32LE(value >>> 0);
	}

	return buffer;
}

// The archive's first 32 bytes: its signature, then where its header is and
// its CRCs (7zFormat.txt's SignatureHeader).
function sevenZipStart(packed: number, header: Uint8Array): Buffer {
	const next = Buffer.concat([
		littleEndian(packed, 8),
		littleEndian(header.length, 8),
		littleEndian(Bun.hash.crc32(header), 4),
	]);

	return Buffer.concat([
		Buffer.from([0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c, 0x00, 0x04]),
		littleEndian(Bun.hash.crc32(next), 4),
		next,
	]);
}

// Every file under `folder`, by its path from it, as an archive names it.
function filesUnder(folder: string, under = folder): string[] {
	return readdirSync(folder, { withFileTypes: true })
		.flatMap((entry) => {
			const path = join(folder, entry.name);

			if (entry.isDirectory()) {
				return filesUnder(path, under);
			}

			return entry.isFile() ? [relative(under, path)] : [];
		})
		.sort();
}

// All of `bytes` into `file`, however many writes it takes.
function writeAll(file: number, bytes: Uint8Array) {
	for (let written = 0; written < bytes.length; ) {
		written += writeSync(file, bytes, written, bytes.length - written);
	}
}

// Windows' stand-in release: Pane's installer as package-win.sh makes one
// (pane/installer/payload.h), the run's own pane_setup.exe with a payload and
// its trailer after it, offered as `offered`. Holding the version it is
// offered as, the payload is the run's app folder with `holds` in its
// pane-version, stored rather than compressed so the VM makes one in seconds:
// the installer's reader takes 7-Zip's Copy as it takes the release's LZMA.
// Holding another, it carries its pane-version alone: Pane refuses it before
// unpacking, as it refuses a dmg holding another Pane.
// What a stand-in installer holds: nothing but its pane-version where it holds
// another version; with `padding`, a Pane that stages and never runs, its
// chrome.exe and helper and the noise; else the whole app folder.
function standInFiles(app: string, offered: boolean, padding: number) {
	if (!offered) {
		return [];
	}

	return padding > 0 ? ["chrome.exe", "pane_setup.exe"] : filesUnder(app);
}

async function standInInstaller(
	app: string,
	work: string,
	holds: string,
	offered: string,
	padding = 0,
): Promise<StandInDmg> {
	const files = standInFiles(app, holds === offered, padding);

	const entries: Stored[] = files
		.filter((name) => name !== "pane-version")
		.map((name) => {
			const path = join(app, name);

			return {
				name: name.split(sep).join("\\"),
				size: statSync(path).size,
				data: () => readFileSync(path),
			};
		});

	if (padding > 0 && holds === offered) {
		const noise = randomBytes(padding);
		entries.push({ name: "padding", size: noise.length, data: () => noise });
	}

	const version = Buffer.from(holds);

	entries.push({
		name: "pane-version",
		size: version.length,
		data: () => version,
	});

	const header = sevenZipHeader(entries);
	const packed = entries.reduce((total, entry) => total + entry.size, 0);
	const start = sevenZipStart(packed, header);
	const program = readFileSync(join(app, "pane_setup.exe"));
	const name = `Pane-${offered}-x64.exe`;
	const path = join(work, name);
	const whole = createHash("sha256");
	const payload = createHash("sha256");
	const file = openSync(path, "w");

	const write = (bytes: Uint8Array, archive: boolean) => {
		writeAll(file, bytes);
		whole.update(bytes);

		if (archive) {
			payload.update(bytes);
		}
	};

	try {
		write(program, false);
		write(start, true);

		for (const entry of entries) {
			if (entry.size > 0) {
				write(entry.data(), true);
			}
		}

		write(header, true);

		// The trailer: the archive's place and size, the folder's unpacked size,
		// the archive's SHA-256, the version, and the magic (payload.h).
		const trailer = Buffer.concat([
			littleEndian(program.length, 8),
			littleEndian(start.length + packed + header.length, 8),
			littleEndian(packed, 8),
			payload.digest(),
			Buffer.concat([version, Buffer.alloc(32 - version.length)]),
			Buffer.from("PanePayload/v1.0"),
		]);

		write(trailer, false);
	} finally {
		closeSync(file);
	}

	return { name, version: offered, path, sha256: whole.digest("hex") };
}

// A stand-in release's file, offered as `offered` and holding a Pane that
// says `holds`: the Mac's dmg, or Windows' installer made from `binary`'s app
// folder; with `padding` bytes of noise, so a slow server's download lasts,
// holding on Windows a Pane that stages and never runs.
export const standInRelease = bySystem({
	mac: (
		_binary: string,
		work: string,
		holds: string,
		offered: string,
		padding = 0,
	) => standInDmg(work, holds, offered, padding),
	windows: (
		binary: string,
		work: string,
		holds: string,
		offered: string,
		padding = 0,
	) => standInInstaller(appOf(binary), work, holds, offered, padding),
});

// The version an app says it is, PaneVersion in the Mac's bundle or Windows'
// pane-version beside chrome.exe, or none.
export const bundleVersion = bySystem({
	mac: (bundle: string): string => {
		const plist = readFileSync(join(bundle, "Contents", "Info.plist"), "utf8");

		return (
			plist.match(/<key>PaneVersion<\/key>\s*<string>([^<]*)<\/string>/)?.[1] ??
			""
		);
	},
	windows: (app: string): string => {
		const file = join(app, "pane-version");

		return existsSync(file) ? readFileSync(file, "utf8").trim() : "";
	},
});

// What an update leaves beside the app while it is staged, or after: the
// Mac's hidden .Pane-<version>.app copies; Windows' update folder's entries
// (pane/installer/update_folder.h), and the helper's copies in the temporary
// folder, which delete themselves a moment after it has run.
export const updateLeftovers = bySystem({
	mac: (bundle: string): string[] =>
		readdirSync(dirname(bundle)).filter((name) => name.startsWith(".Pane-")),
	windows: (app: string): string[] => {
		const update = join(dirname(app), "Update");

		return [
			...(existsSync(update) ? readdirSync(update) : []),
			...readdirSync(tmpdir()).filter((name) =>
				/^pane-update-helper-[0-9a-f]{8}$/i.test(name),
			),
		];
	},
});

// The version staged for `version` beside the app, or none.
export const stagedVersion = bySystem({
	mac: (bundle: string, version: string): string => {
		const copy = join(dirname(bundle), `.Pane-${version}.app`);

		return existsSync(copy) ? bundleVersion(copy) : "";
	},
	windows: (app: string, version: string): string => {
		const copy = join(dirname(app), "Update", version);

		return existsSync(copy) ? bundleVersion(copy) : "";
	},
});

// How the Pane a swap opened shows itself: the Mac's stand-in writes the
// version it holds into `reopened` in the data folder and ends; Windows' is
// the run's own Pane at another version, which answers on the same channel,
// on the same data, its profiles and tabs back.
export const STAND_IN_IS_PANE = bySystem({ mac: false, windows: true });

// Whether a Pane opened from the copy at `bundle`: the Mac's stand-in wrote
// `reopened`; on Windows a process runs the copy's chrome.exe.
export const reopenedFrom = bySystem({
	mac: async (_bundle: string, dataDir: string) =>
		existsSync(join(dataDir, "reopened")),
	windows: async (bundle: string, _dataDir: string) =>
		(
			await pidsMatching(
				executableOf(bundle).replace(/[.*+?^${}()|[\]\\]/g, "\\$&"),
			)
		).length > 0,
});

// The moments of a Restart to update, from a watcher started before the press
// (Windows'): the old window gone, the old browser gone, the app folder at
// `version`, the new browser started from it, and its first visible window,
// each in ms since 1970, polled every 5 ms in a process of its own; null for
// one not seen in a minute. The Mac's are installRelease's
// (update-install.ts).
export type RestartTimeline = {
	windowGone: number | null;
	quit: number | null;
	swapped: number | null;
	started: number | null;
	window: number | null;
	// When Windows' app-starting pointer showed, each time from and to
	// (Paper 11.12.14: it marks the new Pane's start).
	pointer: [number, number][];
};

const RESTART_WATCHER = `
Add-Type -TypeDefinition @'
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
public static class PaneRestart {
	public delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);
	[DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
	[DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle, uint ms);
	[DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
	[DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern bool QueryFullProcessImageName(IntPtr process, uint flags, StringBuilder name, ref uint size);
	[DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr lParam);
	[DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
	[DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
	[DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr hwnd, int attribute, out int value, int size);
	[StructLayout(LayoutKind.Sequential)] struct CursorInfo { public int size; public int flags; public IntPtr cursor; public int x; public int y; }
	[DllImport("user32.dll")] static extern bool GetCursorInfo(ref CursorInfo info);
	[DllImport("user32.dll")] static extern IntPtr LoadCursor(IntPtr instance, IntPtr name);
	static long Now() { return DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(); }
	static string ImageOf(int pid) {
		IntPtr process = OpenProcess(0x1000, false, (uint)pid);
		if (process == IntPtr.Zero) return "";
		var name = new StringBuilder(32768);
		uint size = (uint)name.Capacity;
		bool read = QueryFullProcessImageName(process, 0, name, ref size);
		CloseHandle(process);
		return read ? name.ToString() : "";
	}
	static bool Shows(uint owner) {
		bool shown = false;
		EnumWindows((hwnd, lParam) => {
			uint pid;
			GetWindowThreadProcessId(hwnd, out pid);
			int cloaked;
			if (pid == owner && IsWindowVisible(hwnd) &&
					!(DwmGetWindowAttribute(hwnd, 14, out cloaked, 4) == 0 && cloaked != 0)) {
				shown = true;
				return false;
			}
			return true;
		}, IntPtr.Zero);
		return shown;
	}
	public static string Watch(uint old, string app, string version, string ready) {
		IntPtr browser = OpenProcess(0x00100000, false, old);
		string exe = Path.Combine(app, "chrome.exe");
		long gone = 0, quit = 0, swapped = 0, started = 0, window = 0;
		uint pid = 0;
		IntPtr appStarting = LoadCursor(IntPtr.Zero, (IntPtr)32650);
		var pointer = new StringBuilder();
		long pointerFrom = 0;
		File.WriteAllText(ready, "ready");
		long deadline = Now() + 60000;
		while (Now() < deadline && window == 0) {
			if (gone == 0 && !Shows(old)) gone = Now();
			if (quit == 0 && (browser == IntPtr.Zero || WaitForSingleObject(browser, 0) == 0)) quit = Now();
			if (swapped == 0) {
				try { if (File.ReadAllText(Path.Combine(app, "pane-version")).Trim() == version) swapped = Now(); } catch {}
			}
			if (quit != 0 && started == 0) {
				foreach (var process in Process.GetProcessesByName("chrome")) {
					if (process.Id != old && string.Equals(ImageOf(process.Id), exe, StringComparison.OrdinalIgnoreCase)) {
						started = Now();
						pid = (uint)process.Id;
						break;
					}
				}
			}
			if (started != 0 && Shows(pid)) window = Now();
			var info = new CursorInfo();
			info.size = Marshal.SizeOf(typeof(CursorInfo));
			bool busy = GetCursorInfo(ref info) && info.cursor == appStarting;
			if (busy && pointerFrom == 0) pointerFrom = Now();
			if ((!busy || window != 0) && pointerFrom != 0) {
				pointer.Append((pointer.Length == 0 ? "" : ",") + "[" + pointerFrom + "," + Now() + "]");
				pointerFrom = 0;
			}
			Thread.Sleep(5);
		}
		Func<long, string> at = value => value == 0 ? "null" : value.ToString();
		return "{\\"windowGone\\":" + at(gone) + ",\\"quit\\":" + at(quit) + ",\\"swapped\\":" + at(swapped) + ",\\"started\\":" + at(started) + ",\\"window\\":" + at(window) + ",\\"pointer\\":[" + pointer + "]}";
	}
}
'@
[Console]::Write([PaneRestart]::Watch([uint32]$env:PANE_OLD, $env:PANE_APP, $env:PANE_VERSION, $env:PANE_READY))
`;

// Starts the watcher for a Restart to update of the browser `pid` from the
// copy at `bundle` into `version`, and returns once it is ready, with what it
// will have seen once it has seen the new window or a minute has passed.
// Windows' alone.
export const watchRestart = bySystem({
	mac: async (
		_pid: number,
		_bundle: string,
		_version: string,
		_ready: string,
	): Promise<{ seen: Promise<RestartTimeline> }> => {
		throw new Error("the Mac's restart is timed by installRelease");
	},
	windows: async (
		pid: number,
		bundle: string,
		version: string,
		ready: string,
	): Promise<{ seen: Promise<RestartTimeline> }> => {
		const child = Bun.spawn(
			[
				"powershell.exe",
				"-NoProfile",
				"-NonInteractive",
				"-EncodedCommand",
				Buffer.from(RESTART_WATCHER, "utf16le").toString("base64"),
			],
			{
				env: {
					...process.env,
					PANE_OLD: String(pid),
					PANE_APP: bundle,
					PANE_VERSION: version,
					PANE_READY: ready,
				},
				stdin: "ignore",
				stdout: "pipe",
				stderr: "pipe",
			},
		);

		const seen = (async () => {
			const [output] = await Promise.all([
				new Response(child.stdout).text(),
				child.exited,
			]);

			return JSON.parse(output) as RestartTimeline;
		})();

		await pollValue(
			() => existsSync(ready),
			(there) => there,
			60_000,
			20,
		);

		// Wrapped: an async function's promise of a promise waits for both.
		return { seen };
	},
});

// The executable a copy's Pane runs, and what a launch of it names.
export const executableOf = bySystem({
	mac: (bundle: string) => join(bundle, "Contents", "MacOS", "Pane"),
	windows: (app: string) => join(app, "chrome.exe"),
});

// A copy of the app at `to`: the Mac's bundle, as the Finder copies one;
// Windows' app folder, file by file.
const copyApp = bySystem({
	mac: (from: string, to: string) => copyBundle(from, to),
	windows: async (from: string, to: string) => {
		cpSync(from, to, { recursive: true });
	},
});

// Where a copy of the app goes in a folder of its own: the Mac's Pane.app;
// Windows' Pane\Application, as an install lays it out, its update folder
// beside it.
const copyPlace = bySystem({
	mac: (folder: string) => join(folder, "Pane.app"),
	windows: (folder: string) => join(folder, "Pane", "Application"),
});

// Makes a copy report 0.0.1: the Mac's PaneVersion, its signature made again
// with what it had (the hardened runtime, its entitlements), as the live update
// check makes a published Pane older; Windows' pane-version, which nothing
// signs yet (U6).
const makeOlder = bySystem({
	mac: async (bundle: string) => {
		await setPlistString(
			join(bundle, "Contents", "Info.plist"),
			"PaneVersion",
			"0.0.1",
		);

		await signAdHoc(bundle, true);
	},
	windows: async (app: string) => {
		writeFileSync(join(app, "pane-version"), "0.0.1");
	},
});

// What else a copy's launch names: on Windows that a scenario's copy installs
// its update (PANE_UPDATE_INSTALL=1), which only a copy in the temporary
// folder takes, never the installed Pane the VM's runs start from
// (pane/updates/update_installer_win.cc); the Mac's copies always do.
const installsItself = bySystem({
	mac: {} as Record<string, string>,
	windows: { PANE_UPDATE_INSTALL: "1" } as Record<string, string>,
});

// A copy of the run's Pane in a folder of its own, checking `releases`, with
// `env` too. Given `olderThan`, a copy that reports that version or a later
// one is made to report 0.0.1.
export async function launchCopy(
	context: ScenarioContext,
	releases: string,
	olderThan?: string,
	env: Record<string, string> = {},
) {
	const bundle = copyPlace(context.tempDir("pane-apps-"));
	await copyApp(appOf(context.binary), bundle);

	if (olderThan && !isOlder(bundleVersion(bundle) || "0.0.0", olderThan)) {
		await makeOlder(bundle);
	}

	const downloads = context.tempDir("pane-downloads-");

	const app = await launchPane(context, {
		binary: executableOf(bundle),
		env: {
			PANE_UPDATE_URL: releases,
			PANE_UPDATE_DOWNLOADS: downloads,
			PANE_UPDATE_OPEN: "0",
			...installsItself,
			...env,
		},
	});

	return { app, bundle, downloads };
}

// Whether version `a` comes before `b`, part by part.
function isOlder(a: string, b: string): boolean {
	const left = a.split(".").map(Number);
	const right = b.split(".").map(Number);

	for (let part = 0; part < Math.max(left.length, right.length); part++) {
		const difference = (left[part] ?? 0) - (right[part] ?? 0);

		if (difference !== 0) {
			return difference < 0;
		}
	}

	return false;
}
