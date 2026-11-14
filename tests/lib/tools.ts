// The system's tools the runner calls, each with the Mac's side and Windows':
// the Swift tools in tools/, Caps Lock, a window's capture and recording, the
// user's idle time and front app, the system's appearance, the data volume's
// disk images (ADR 0113), LaunchServices, what makes a stand-in Pane (a copy,
// a signature, a plist, a dmg), a download's quarantine and origin, a crash's
// dumps and Crashpad's database, the data folder's privacy and its index, and
// a search of files' bytes.
import {
	closeSync,
	type Dirent,
	existsSync,
	mkdirSync,
	openSync,
	readdirSync,
	readSync,
	realpathSync,
	statSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import type { Subprocess } from "bun";
import { bySystem, noWindowsSide, powershell } from "./platform";

const BINARIES = join(tmpdir(), "pane-tests");

// Windows' desktop, for a PowerShell script of a Windows side: user32's
// windows, input time, and a window's own drawing, through a class of C#
// (PaneDesktop) the script can call. Coordinates are the screen's pixels: the
// script's process is made DPI aware first.
const DESKTOP = `
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;
public static class PaneDesktop {
	public delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);
	[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
	[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr lParam);
	[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
	[DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
	[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
	[DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr hwnd, int index);
	[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
	[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
	[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
	[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
	[DllImport("user32.dll")] public static extern bool GetLastInputInfo(ref LASTINPUTINFO info);
	[DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr hwnd, int attribute, out int value, int size);
	public struct RECT { public int Left, Top, Right, Bottom; }
	public struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }

	// The visible top-level windows of a normal layer (no tool window: menus,
	// tooltips), on screen (not minimized, not cloaked), of these processes.
	public static List<object> WindowsOf(uint[] pids) {
		var found = new List<object>();
		EnumWindows((hwnd, lParam) => {
			uint pid;
			GetWindowThreadProcessId(hwnd, out pid);
			int cloaked;
			if (Array.IndexOf(pids, pid) < 0 || !IsWindowVisible(hwnd) || IsIconic(hwnd) ||
					(GetWindowLong(hwnd, -20) & 0x80) != 0 ||
					(DwmGetWindowAttribute(hwnd, 14, out cloaked, 4) == 0 && cloaked != 0)) {
				return true;
			}
			var title = new StringBuilder(512);
			GetWindowText(hwnd, title, title.Capacity);
			RECT rect;
			GetWindowRect(hwnd, out rect);
			string owner = "";
			try { owner = System.Diagnostics.Process.GetProcessById((int)pid).ProcessName; } catch {}
			found.Add(new Dictionary<string, object> {
				{ "id", hwnd.ToInt64() }, { "pid", pid }, { "owner", owner }, { "title", title.ToString() },
				{ "bounds", new Dictionary<string, object> {
					{ "X", rect.Left }, { "Y", rect.Top },
					{ "Width", rect.Right - rect.Left }, { "Height", rect.Bottom - rect.Top } } } });
			return true;
		}, IntPtr.Zero);
		return found;
	}

	// The window drawn by itself into a PNG, whatever covers it.
	public static bool Capture(IntPtr hwnd, string file) {
		RECT rect;
		if (!GetWindowRect(hwnd, out rect) || rect.Right <= rect.Left || rect.Bottom <= rect.Top) {
			return false;
		}
		using (var bitmap = new Bitmap(rect.Right - rect.Left, rect.Bottom - rect.Top, PixelFormat.Format32bppArgb)) {
			using (var graphics = Graphics.FromImage(bitmap)) {
				IntPtr hdc = graphics.GetHdc();
				bool drawn = PrintWindow(hwnd, hdc, 2);
				graphics.ReleaseHdc(hdc);
				if (!drawn) {
					return false;
				}
			}
			bitmap.Save(file, ImageFormat.Png);
		}
		return true;
	}

	public static uint ForegroundPid() {
		uint pid = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), out pid);
		return pid;
	}

	public static double IdleSeconds() {
		var info = new LASTINPUTINFO();
		info.cbSize = (uint)Marshal.SizeOf(info);
		GetLastInputInfo(ref info);
		return unchecked((uint)Environment.TickCount - info.dwTime) / 1000.0;
	}
}
'@
[void][PaneDesktop]::SetProcessDPIAware()
[Console]::OutputEncoding = [Text.Encoding]::UTF8
`;

// What `script` prints, with PaneDesktop (DESKTOP) to call.
export function windowsDesktop(
	script: string,
	env: Record<string, string> = {},
): Promise<string> {
	return powershell(`${DESKTOP}\n${script}`, env);
}

// The Swift tool `name`'s binary, compiled on first use, and again once its
// source is newer. They are the Mac's: Windows' sides of what they read or
// do sit in the modules that call them, or wait on the item named there.
export const swiftTool = bySystem({
	mac: async (name: string): Promise<string> => {
		const source = join(import.meta.dir, "..", "tools", `${name}.swift`);
		const binary = join(BINARIES, name);

		if (
			!existsSync(binary) ||
			statSync(binary).mtimeMs < statSync(source).mtimeMs
		) {
			mkdirSync(BINARIES, { recursive: true });
			await Bun.$`swiftc -O ${source} -o ${binary}`.quiet();
		}

		return binary;
	},
	windows: async (name: string): Promise<string> => {
		throw new Error(`tests/tools/${name}.swift is the Mac's`);
	},
});

// Whether the system draws its Caps Lock indicator by a focused field, which
// the window checks read as a stray window: the Mac does while Caps Lock is
// on; Windows draws none by a field of Chromium's own.
export const capsLockIndicator = bySystem({
	mac: async () =>
		(await Bun.$`${await swiftTool("caps-lock")}`.text()).trim() === "on",
	windows: async () => false,
});

// The window numbered `windowId` (the window server's number on the Mac, the
// window's handle on Windows) drawn into the PNG `file`, without its shadow,
// whatever covers it: whether it was. Windows has the window draw itself
// (PrintWindow, its full content).
export const captureWindow = bySystem({
	mac: async (windowId: number, file: string) =>
		(
			await Bun.$`/usr/sbin/screencapture -x -o -l ${windowId} ${file}`
				.quiet()
				.nothrow()
		).exitCode === 0,
	windows: async (windowId: number, file: string) =>
		(
			await windowsDesktop(
				"[Console]::Write([PaneDesktop]::Capture([IntPtr][long]$env:PANE_WINDOW, $env:PANE_FILE))",
				{ PANE_WINDOW: String(windowId), PANE_FILE: file },
			).catch(() => "False")
		).trim() === "True",
});

// The window recorded into the movie `file` for `seconds`, from now: the
// process records until it exits. Frames are a real PC's on Windows (U6):
// the VM has no GPU.
export const recordWindow = bySystem({
	mac: (windowId: number, seconds: string, file: string): Subprocess =>
		Bun.spawn(
			[
				"/usr/sbin/screencapture",
				"-x",
				"-v",
				"-V",
				seconds,
				`-l${windowId}`,
				file,
			],
			{ stdout: "ignore", stderr: "ignore" },
		),
	windows: (_windowId: number, _seconds: string, _file: string): Subprocess => {
		throw noWindowsSide("a window's recording", "U6", "a real PC's frames");
	},
});

// Seconds since the user's last key, click, or move.
export const idleSeconds = bySystem({
	mac: async () => {
		const nanoseconds = /"HIDIdleTime" = (\d+)/.exec(
			await Bun.$`ioreg -c IOHIDSystem`.quiet().text(),
		)?.[1];

		return Number(nanoseconds) / 1e9;
	},
	windows: async () =>
		Number(
			(
				await windowsDesktop("[Console]::Write([PaneDesktop]::IdleSeconds())")
			).trim(),
		),
});

// The pid of the app in front, which the user's keys reach.
export const frontmostPid = bySystem({
	mac: async () => {
		const front = (await Bun.$`lsappinfo front`.quiet().text()).trim();

		const pid = /\bpid"?\s*=\s*(\d+)/.exec(
			await Bun.$`lsappinfo info -only pid ${front}`.quiet().text(),
		)?.[1];

		return Number(pid);
	},
	windows: async () =>
		Number(
			(
				await windowsDesktop("[Console]::Write([PaneDesktop]::ForegroundPid())")
			).trim(),
		),
});

// Whether the system's appearance is dark: the Mac's (AppleInterfaceStyle),
// or the mode Windows gives apps (AppsUseLightTheme 0).
export const systemIsDark = bySystem({
	mac: async () =>
		(
			await Bun.$`defaults read -g AppleInterfaceStyle`.quiet().nothrow().text()
		).trim() === "Dark",
	windows: async () => {
		const query = Bun.spawn(
			[
				"reg",
				"query",
				"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
				"/v",
				"AppsUseLightTheme",
			],
			{ stdout: "pipe", stderr: "ignore" },
		);

		return /AppsUseLightTheme\s+REG_DWORD\s+0x0\s*$/m.test(
			await new Response(query.stdout).text(),
		);
	},
});

type AttachedImage = {
	"image-path"?: string;
	"system-entities"?: {
		"dev-entry"?: string;
		"content-hint"?: string;
	}[];
};

// The devices of the disk images attached from inside `path`: a scenario's
// data volume (ADR 0113). hdiutil names an image by the path it was attached
// with, /var or /private/var, so both sides are resolved. Windows has no
// volume (U5): what checks one there waits on its Windows design.
export const imagesAttachedIn = bySystem({
	mac: async (path: string): Promise<string[]> => {
		if (!existsSync(path)) {
			return [];
		}

		const inside = `${realpathSync(path)}/`;

		const resolved = (image: string) =>
			existsSync(image) ? realpathSync(image) : image;

		const info = await Bun.$`hdiutil info -plist | plutil -convert json -o - -`
			.quiet()
			.nothrow();

		const images =
			(
				JSON.parse(info.stdout.toString() || "{}") as {
					images?: AttachedImage[];
				}
			).images ?? [];

		return images
			.filter((image) => {
				const imagePath = image["image-path"];

				return (
					imagePath !== undefined && resolved(imagePath).startsWith(inside)
				);
			})
			.map((image) => imageDevice(image["system-entities"] ?? []))
			.filter((device): device is string => device !== undefined);
	},
	windows: async (_path: string): Promise<string[]> => {
		throw noWindowsSide(
			"the data volume's disk images",
			"U5",
			"Windows has no volume",
		);
	},
});

// An attached image's own device, from hdiutil's entities: its whole disk,
// not its APFS container's, a whole disk too. hdiutil lists them in no set
// order (DataVolume).
export function imageDevice(
	entities: { "dev-entry"?: string; "content-hint"?: string }[],
): string | undefined {
	return entities.find(
		(entity) =>
			/^\/dev\/disk\d+$/.test(entity["dev-entry"] ?? "") &&
			![
				"Apple_APFS_Container",
				"EF57347C-0000-11AA-AA11-00306543ECAC",
			].includes(entity["content-hint"] ?? ""),
	)?.["dev-entry"];
}

// Detaches them: a Pane killed leaves its volume attached until its
// watchdog detaches it, and killUsing would kill that watchdog, whose
// arguments name the image. Windows' Pane attaches none (U5), so a
// teardown there has nothing to detach.
export const detachImagesIn = bySystem({
	mac: async (path: string) => {
		for (const device of await imagesAttachedIn(path)) {
			await Bun.$`hdiutil detach ${device} -force -quiet`.quiet().nothrow();
		}
	},
	windows: async (_path: string) => {},
});

// Whether a volume is mounted at `path`, which the table may name under
// /private. Windows has no volume (U5).
export const mountedAt = bySystem({
	mac: async (path: string) => {
		const table = await Bun.$`/sbin/mount`.text();

		return (
			table.includes(` on ${path} `) || table.includes(` on /private${path} `)
		);
	},
	windows: async (_path: string): Promise<boolean> => {
		throw noWindowsSide(
			"the data volume's mount",
			"U5",
			"Windows has no volume",
		);
	},
});

const LSREGISTER =
	"/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister";

// A launch registers the bundle with LaunchServices, which opens the newest
// copy of a bundle identifier it knows: a test copy newer than the installed
// Pane would be the one a link opens, on the user's own data. A run forgets
// the copy it ran when it ends; one in /Applications is the user's. A launch
// on Windows registers nothing a link opens: Chromium is registered there by
// its installer (W7) or as the default browser (W8), neither of which a
// scenario runs.
export const forgetBundle = bySystem({
	mac: async (binary: string) => {
		const bundle = binary.replace(/\/Contents\/MacOS\/[^/]+$/, "");

		if (bundle === binary || bundle.startsWith("/Applications/")) {
			return;
		}

		await Bun.$`${LSREGISTER} -u ${bundle}`.quiet().nothrow();
	},
	windows: async (_binary: string) => {},
});

// What makes a stand-in Pane, and a copy of one that reports another
// version: the Mac's bundles and dmgs. Windows' install and update are W7's.
const INSTALL = "Windows' install and update decide it";

// A copy of the bundle `from` made at `to`, as the Finder copies one.
export const copyBundle = bySystem({
	mac: async (from: string, to: string) => {
		await Bun.$`ditto ${from} ${to}`.quiet();
	},
	windows: async (_from: string, _to: string): Promise<void> => {
		throw noWindowsSide("a copy of an app bundle", "W7", INSTALL);
	},
});

// The bundle signed again, ad hoc; `keep` keeps its identifier, entitlements,
// flags, and hardened runtime, as a release's signature has them.
export const signAdHoc = bySystem({
	mac: async (bundle: string, keep = false) => {
		await Bun.$`codesign --force --sign - ${keep ? ["--preserve-metadata=identifier,entitlements,flags,runtime"] : []} ${bundle}`.quiet();
	},
	windows: async (_bundle: string, _keep = false): Promise<void> => {
		throw noWindowsSide("an ad hoc signature", "W7", INSTALL);
	},
});

// The plist's string `key` (a dotted path, plutil's), or nothing.
export const plistString = bySystem({
	mac: (plist: string, key: string) => {
		const read = Bun.spawnSync([
			"/usr/bin/plutil",
			"-extract",
			key,
			"raw",
			"-o",
			"-",
			plist,
		]);

		return read.exitCode === 0 ? read.stdout.toString().trim() : undefined;
	},
	windows: (_plist: string, _key: string): string | undefined => {
		throw noWindowsSide(
			"a bundle's plist",
			"W7",
			"no Pane was published for Windows",
		);
	},
});

// The plist's string `key` set to `value`.
export const setPlistString = bySystem({
	mac: async (plist: string, key: string, value: string) => {
		await Bun.$`/usr/libexec/PlistBuddy -c ${`Set :${key} ${value}`} ${plist}`.quiet();
	},
	windows: async (
		_plist: string,
		_key: string,
		_value: string,
	): Promise<void> => {
		throw noWindowsSide("a bundle's plist", "W7", INSTALL);
	},
});

// The C program `source` built into `binary`, with `defines` (-D).
export const compileC = bySystem({
	mac: async (source: string, binary: string, defines: string[]) => {
		await Bun.$`clang -O2 ${defines.map((define) => `-D${define}`)} -o ${binary} ${source}`.quiet();
	},
	windows: async (
		_source: string,
		_binary: string,
		_defines: string[],
	): Promise<void> => {
		throw noWindowsSide("a stand-in Pane's executable", "W7", INSTALL);
	},
});

// A dmg at `path` holding the folder `root`, its volume named `name`.
export const makeDmg = bySystem({
	mac: async (root: string, path: string, name: string) => {
		await Bun.$`hdiutil create -quiet -ov -fs HFS+ -volname ${name} -srcfolder ${root} ${path}`.quiet();
	},
	windows: async (
		_root: string,
		_path: string,
		_name: string,
	): Promise<void> => {
		throw noWindowsSide("a dmg", "W7", INSTALL);
	},
});

// Whether the system marks `file` as come from elsewhere: the Mac's
// quarantine (com.apple.quarantine), Windows' Mark of the Web (its
// Zone.Identifier stream).
export const isQuarantined = bySystem({
	mac: async (file: string) =>
		(await Bun.$`xattr -p com.apple.quarantine ${file}`.quiet().nothrow())
			.exitCode === 0,
	windows: async (file: string) =>
		(
			await powershell(
				"if (Get-Item -LiteralPath $env:PANE_FILE -Stream Zone.Identifier -ErrorAction SilentlyContinue) { 'marked' }",
				{ PANE_FILE: file },
			)
		).trim() === "marked",
});

// What a downloaded file keeps of where it came from: on the Mac its extended
// attributes' names, com.apple.quarantine, and kMDItemWhereFroms where
// Chromium wrote the page's and the file's URLs, which Spotlight indexes; on
// Windows its Mark of the Web, the Zone.Identifier stream's lines (ZoneId,
// HostUrl, ReferrerUrl), whose page Pane drops (ADR 0115). `marked`: the
// system takes it as come from elsewhere; `namesAPage`: it keeps an address.
export const downloadOrigin = bySystem({
	mac: async (file: string) => {
		const kept = (await Bun.$`/usr/bin/xattr ${file}`.quiet().nothrow().text())
			.trim()
			.split("\n")
			.filter(Boolean);

		return {
			marked: kept.includes("com.apple.quarantine"),
			namesAPage: kept.includes("com.apple.metadata:kMDItemWhereFroms"),
			kept,
		};
	},
	windows: async (file: string) => {
		const kept = (
			await powershell(
				"Get-Content -LiteralPath $env:PANE_FILE -Stream Zone.Identifier -ErrorAction SilentlyContinue",
				{ PANE_FILE: file },
			)
		)
			.trim()
			.split(/\r?\n/)
			.filter(Boolean);

		return {
			marked: kept.includes("ZoneId=3"),
			namesAPage: kept.some(
				(line) =>
					/^(HostUrl|ReferrerUrl)=/.test(line) &&
					line !== "HostUrl=about:internet",
			),
			kept,
		};
	},
});

// The crash dumps (`*.dmp`) under `folders`.
export const crashDumpsUnder = bySystem({
	mac: async (folders: string[]) =>
		(
			await Bun.$`/usr/bin/find ${folders} -name ${"*.dmp"}`
				.quiet()
				.nothrow()
				.text()
		)
			.trim()
			.split("\n")
			.filter(Boolean),
	windows: async (folders: string[]) =>
		folders.flatMap((folder) =>
			[...filesUnder(folder)].filter((path) => path.endsWith(".dmp")),
		),
});

// What Chromium's crash handler, Crashpad, keeps in its database since
// `since` (ms): on the Mac under the home's Application Support, a test
// copy's home its own, so all of it; on Windows, where chrome_elf would start
// it before any code of Pane's, reading the user's own local application
// data from the shell, in the user data folder its install mode names there
// (`%LOCALAPPDATA%\Pane\User Data`, patch 0001), which the user's own Pane
// shares, so what is newer.
export const crashpadKept = bySystem({
	mac: (home: string, _since: number): string[] => {
		const folder = join(
			home,
			"Library",
			"Application Support",
			"Chromium",
			"Crashpad",
		);

		return existsSync(folder) ? readdirSync(folder) : [];
	},
	windows: (_home: string, since: number): string[] => {
		const folder = join(
			process.env.LOCALAPPDATA ?? "",
			"Pane",
			"User Data",
			"Crashpad",
		);

		return [...filesUnder(folder)].filter(
			(path) => statSync(path).mtimeMs >= since,
		);
	},
});

// Whether the data folder is the user's alone: on the Mac by its mode, 0700,
// which Pane sets; on Windows by its ACL, which it takes from the user's
// profile it lies in, Pane setting none: the user, SYSTEM, and the
// Administrators, who read every file anyway. `kept` is what was read.
export const dataFolderPrivacy = bySystem({
	mac: async (dataDir: string) => {
		const kept = (statSync(dataDir).mode & 0o777).toString(8);

		return { alone: kept === "700", kept };
	},
	windows: async (dataDir: string) => {
		const rules = (
			await powershell(
				`$user = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
foreach ($rule in (Get-Acl -LiteralPath $env:PANE_DATA).Access) {
	$sid = $rule.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value
	if ($sid -eq $user) { $sid = 'the user' }
	"$($rule.AccessControlType) $sid"
}`,
				{ PANE_DATA: dataDir },
			)
		)
			.trim()
			.split(/\r?\n/)
			.filter(Boolean);

		// SYSTEM and the Administrators, by their well-known SIDs.
		const allowed = ["Allow the user", "Allow S-1-5-18", "Allow S-1-5-32-544"];

		return {
			alone: rules.every(
				(rule) => rule.startsWith("Deny ") || allowed.includes(rule),
			),
			kept: rules.join(", "),
		};
	},
});

// What of Pane's data an indexer may take (ADR 0121): on the Mac, the items
// Spotlight has of the mounted volume, whose root says .metadata_never_index
// at every mount (`refused`); on Windows, which has no volume (U5), the data
// folder carries the not-indexed attribute, which NTFS gives what is made in
// it after, and every item under it without it. The runner's own folders,
// made before Pane's launch (`automation`, `home`), aren't Pane's.
export const indexOfTheData = bySystem({
	mac: async (dataDir: string) => {
		const mount = join(dataDir, "Engine", "Volume");

		return {
			refused: existsSync(join(mount, ".metadata_never_index")),
			indexable: (
				await Bun.$`/usr/bin/mdfind -onlyin ${mount} kMDItemFSName == "*"`
					.nothrow()
					.quiet()
					.text()
			)
				.split("\n")
				.filter(Boolean),
		};
	},
	windows: async (dataDir: string) => {
		const [root = "", ...without] = (
			await powershell(
				`$flag = [IO.FileAttributes]::NotContentIndexed
[bool]((Get-Item -LiteralPath $env:PANE_DATA -Force).Attributes -band $flag)
Get-ChildItem -LiteralPath $env:PANE_DATA -Recurse -Force -ErrorAction SilentlyContinue |
	Where-Object { -not ($_.Attributes -band $flag) } |
	ForEach-Object { $_.FullName.Substring($env:PANE_DATA.Length + 1) }`,
				{ PANE_DATA: dataDir },
			)
		)
			.trim()
			.split(/\r?\n/);

		return {
			refused: root === "True",
			indexable: without.filter(
				(path) => path && !/^(automation|home)(\\|$)/.test(path),
			),
		};
	},
});

// Every file under `folder`, as grep -r walks it: no link followed, no folder
// it can't read, and none whose name starts with `skipFolders`.
function* filesUnder(folder: string, skipFolders?: string): Generator<string> {
	let entries: Dirent[];

	try {
		entries = readdirSync(folder, { withFileTypes: true });
	} catch {
		return;
	}

	for (const entry of entries) {
		const path = join(folder, entry.name);

		if (entry.isFile()) {
			yield path;
		} else if (
			entry.isDirectory() &&
			!(skipFolders !== undefined && entry.name.startsWith(skipFolders))
		) {
			yield* filesUnder(path, skipFolders);
		}
	}
}

// The files under `root` whose bytes hold any of `texts`, as grep -rlaF finds
// them, but those in a folder whose name starts with `skipFolders`. Only
// files: a FIFO, which another app's tools leave in the temporary folder
// (Xcode's ibtoold), holds nothing at rest, and opening one waits for its
// writer for good (-D skip). Windows reads each file itself, passing over one
// it can't read (open in another app), as grep does.
export const filesHolding = bySystem({
	mac: async (root: string, texts: string[], skipFolders?: string) => {
		const patterns = texts.flatMap((text) => ["-e", text]);
		const skip = skipFolders ? [`--exclude-dir=${skipFolders}*`] : [];

		return (
			await Bun.$`/usr/bin/grep -rlaF -D skip ${patterns} ${skip} ${root}`
				.quiet()
				.nothrow()
				.text()
		)
			.split("\n")
			.filter(Boolean);
	},
	windows: async (root: string, texts: string[], skipFolders?: string) => {
		const needles = texts.map((text) => Buffer.from(text));
		const found: string[] = [];

		for (const file of filesUnder(root, skipFolders)) {
			try {
				if (holdsAny(file, needles)) {
					found.push(file);
				}
			} catch {}
		}

		return found;
	},
});

// Whether `file`'s bytes hold any of `needles`, read a megabyte at a time,
// each read overlapping the last by a needle's length, as grep streams a
// file: the temporary folder holds files of gigabytes.
function holdsAny(file: string, needles: Buffer[]): boolean {
	const overlap = Math.max(0, ...needles.map((needle) => needle.length - 1));
	const buffer = Buffer.alloc(1 << 20);
	const descriptor = openSync(file, "r");

	try {
		let kept = 0;

		for (;;) {
			const read = readSync(
				descriptor,
				buffer,
				kept,
				buffer.length - kept,
				null,
			);

			if (read === 0) {
				return false;
			}

			const window = buffer.subarray(0, kept + read);

			if (needles.some((needle) => window.includes(needle))) {
				return true;
			}

			kept = Math.min(overlap, window.length);
			window.copyWithin(0, window.length - kept);
		}
	} finally {
		closeSync(descriptor);
	}
}
