// Pane's install, registration, and uninstall on Windows as its user meets
// them (ADR 0127; Paper 11.12.3 to 11.12.10, 11.12.16), which no scenario's
// launch can: the installer downloaded with its Mark of the Web and started
// through the shell, past SmartScreen; its window closing as Pane's first
// window paints; Start, the desktop, Default apps, and Installed apps as
// Windows lists Pane, with Pane's icon and AppUserModelID; the toast
// activator; a reinstall over an open Pane, asked to exit as Alt+F4 asks; the
// App icon's sky reaching the shortcuts, at a change and at launch; a PIN set
// up in Settings; the uninstall from Installed apps keeping the data, which a
// reinstall finds behind its PIN; an installer older than the Pane
// installed, refused; the uninstall erasing the data, after which nothing
// of Pane's is left in %LOCALAPPDATA%, %APPDATA%, %TEMP%, HKCU, Start, the
// taskbar, or the desktop; and an uninstall with no data, which says nothing
// of data and leaves nothing either. What a screen reader reads of the install window
// is read through UI Automation's own client, as Narrator reads it, and
// every press goes through it too; Windows' screens and Pane's windows are
// captured into the run's folder.
//
// The test VM's alone (docs/building.md, On Windows), as tests/vm/
// user-launch.ts is: it installs for the VM's own user, refuses on any other
// computer and wherever Pane is installed or its data is there already, and
// leaves nothing of Pane's. Real input goes to the VM's screen only, each key
// to a window proven in front first, never to the Mac's. Run in the console's
// session: bun tests/run.ts --binary <folder>\Pane-<version>-x64.exe --out
// <dir> --timeout 1800 tests/vm/windows-install.ts
import {
	copyFileSync,
	existsSync,
	readdirSync,
	rmSync,
	writeFileSync,
} from "node:fs";
import { join } from "node:path";
import { bySystem, powershell } from "../lib/platform";
import { expect, type Scenario } from "../run";

// The test VM, by its disk's serial (scripts/start.sh in the VM's folder:
// serial=PANEVM) and its machine's maker.
const inTheTestVM = bySystem({
	mac: async (): Promise<boolean> => false,
	windows: async (): Promise<boolean> => {
		const found = await powershell(
			"$disks = (Get-CimInstance Win32_DiskDrive).SerialNumber -join ','; $maker = (Get-CimInstance Win32_ComputerSystem).Manufacturer; \"$disks|$maker\"",
		);

		return /PANEVM/.test(found) && /QEMU/i.test(found);
	},
});

// The toast activator's CLSID, the install mode's (chrome/install_static).
const TOAST_ACTIVATOR = "{643681EE-CBD6-4AA1-BFCF-837FC0A172E0}";
const PIN = "1234";

// Shortcuts' and windows' properties, the top-level windows of a process, the
// foreground, and UI Automation's own client (uiautomationcore, through COM,
// as Narrator, NVDA, and JAWS use it; the managed client reads Win32
// controls as panes). Windows PowerShell's Add-Type compiles C# 5.
const HELPERS = String.raw`using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class W7b {
  [StructLayout(LayoutKind.Sequential, Pack = 4)] public struct Key { public Guid fmtid; public uint pid; }
  [StructLayout(LayoutKind.Explicit)] public struct Variant { [FieldOffset(0)] public ushort vt; [FieldOffset(8)] public IntPtr p; [FieldOffset(8)] public Guid guid; }
  [ComImport, Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IPropertyStore {
    int GetCount(out uint count); int GetAt(uint index, out Key key);
    int GetValue(ref Key key, out Variant value); int SetValue(ref Key key, ref Variant value); int Commit();
  }
  [ComImport, Guid("30cbe57d-d9d0-452a-ab13-7ac5ac4825ee"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IUIAutomation {
    void CompareElements(); void CompareRuntimeIds(); void GetRootElement();
    [PreserveSig] int ElementFromHandle(IntPtr hwnd, out IUIAutomationElement element);
    void ElementFromPoint(); void GetFocusedElement(); void GetRootElementBuildCache(); void ElementFromHandleBuildCache();
    void ElementFromPointBuildCache(); void GetFocusedElementBuildCache(); void CreateTreeWalker();
    void get_ControlViewWalker(); void get_ContentViewWalker();
    [PreserveSig] int get_RawViewWalker(out IUIAutomationTreeWalker walker);
  }
  [ComImport, Guid("d22108aa-8ac5-49a5-837b-37bbb3d7591e"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IUIAutomationElement {
    void SetFocus(); void GetRuntimeId(); void FindFirst(); void FindAll(); void FindFirstBuildCache(); void FindAllBuildCache(); void BuildUpdatedCache();
    [PreserveSig] int GetCurrentPropertyValue(int propertyId, [MarshalAs(UnmanagedType.Struct)] out object value);
    void GetCurrentPropertyValueEx(); void GetCachedPropertyValue(); void GetCachedPropertyValueEx(); void GetCurrentPatternAs(); void GetCachedPatternAs();
    [PreserveSig] int GetCurrentPattern(int patternId, [MarshalAs(UnmanagedType.IUnknown)] out object pattern);
  }
  [ComImport, Guid("4042c624-389c-4afc-a630-9df854a541fc"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IUIAutomationTreeWalker {
    void GetParentElement();
    [PreserveSig] int GetFirstChildElement(IUIAutomationElement element, out IUIAutomationElement first);
    void GetLastChildElement();
    [PreserveSig] int GetNextSiblingElement(IUIAutomationElement element, out IUIAutomationElement next);
  }
  [ComImport, Guid("fb377fbe-8ea6-46d5-9c73-6499642d3059"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IUIAutomationInvokePattern { [PreserveSig] int Invoke(); }
  [ComImport, Guid("94cf8058-9b8d-4ab9-8bfd-4cd0a33c8c70"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IUIAutomationTogglePattern { [PreserveSig] int Toggle(); }
  [ComImport, Guid("a98b4cc4-0d7c-45c6-8ae1-5ba69d43f6aa"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IUIAutomationSelectionItemPattern { [PreserveSig] int Select(); }
  [ComImport, Guid("619be086-1f4e-4ee4-bafa-210128738730"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IUIAutomationExpandCollapsePattern { [PreserveSig] int Expand(); }
  [ComImport, Guid("a94cd8b1-0844-4cd6-9d2d-640537ab39e9"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IUIAutomationValuePattern { [PreserveSig] int SetValue([MarshalAs(UnmanagedType.BStr)] string value); }
  [DllImport("shell32.dll")] static extern int SHGetPropertyStoreForWindow(IntPtr hwnd, ref Guid iid, [MarshalAs(UnmanagedType.Interface)] out IPropertyStore store);
  [DllImport("shell32.dll", CharSet = CharSet.Unicode)] static extern int SHGetPropertyStoreFromParsingName(string path, IntPtr bind, int flags, ref Guid iid, [MarshalAs(UnmanagedType.Interface)] out IPropertyStore store);
  public delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr lParam);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr hwnd, StringBuilder text, int count);
  [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr hwnd);
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr hwnd);
  [DllImport("user32.dll")] static extern bool AttachThreadInput(uint from, uint to, bool attach);
  [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
  [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr hwnd, int attribute, out Rect rect, int size);
  static readonly Guid AppModel = new Guid("9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3");
  static string Read(IPropertyStore store, uint pid) {
    Key key = new Key { fmtid = AppModel, pid = pid };
    Variant value;
    if (store.GetValue(ref key, out value) != 0) return "";
    if (value.vt == 31) return Marshal.PtrToStringUni(value.p);
    if (value.vt == 72) return Marshal.PtrToStructure<Guid>(value.p).ToString("B").ToUpperInvariant();
    return "";
  }
  // System.AppUserModel.ID (5) and .ToastActivatorCLSID (26) of a shortcut.
  public static string LinkAppId(string path) { return LinkProperty(path, 5); }
  public static string LinkToastActivator(string path) { return LinkProperty(path, 26); }
  static string LinkProperty(string path, uint pid) {
    Guid iid = typeof(IPropertyStore).GUID; IPropertyStore store;
    return SHGetPropertyStoreFromParsingName(path, IntPtr.Zero, 0, ref iid, out store) == 0 ? Read(store, pid) : "";
  }
  public static string WindowAppId(long hwnd) {
    Guid iid = typeof(IPropertyStore).GUID; IPropertyStore store;
    return SHGetPropertyStoreForWindow(new IntPtr(hwnd), ref iid, out store) == 0 ? Read(store, 5) : "";
  }
  // The visible top-level windows of a process: handle, class, title.
  public static List<string> Windows(int pid) {
    List<string> found = new List<string>();
    EnumWindows((hwnd, lp) => {
      uint owner; GetWindowThreadProcessId(hwnd, out owner);
      if (owner == pid && IsWindowVisible(hwnd)) {
        StringBuilder cls = new StringBuilder(256); GetClassName(hwnd, cls, 256);
        StringBuilder title = new StringBuilder(512); GetWindowText(hwnd, title, 512);
        found.Add(hwnd.ToInt64() + "|" + cls + "|" + title);
      }
      return true;
    }, IntPtr.Zero);
    return found;
  }
  // The first visible top-level window with this title: handle, class,
  // title (Settings' is ApplicationFrameHost's, not SystemSettings').
  public static string Titled(string wanted) {
    string found = "";
    EnumWindows((hwnd, lp) => {
      if (!IsWindowVisible(hwnd)) return true;
      StringBuilder title = new StringBuilder(512); GetWindowText(hwnd, title, 512);
      if (title.ToString() != wanted) return true;
      StringBuilder cls = new StringBuilder(256); GetClassName(hwnd, cls, 256);
      found = hwnd.ToInt64() + "|" + cls + "|" + title;
      return false;
    }, IntPtr.Zero);
    return found;
  }
  public static Rect Frame(long hwnd) {
    Rect rect; DwmGetWindowAttribute(new IntPtr(hwnd), 9, out rect, Marshal.SizeOf(typeof(Rect)));
    return rect;
  }
  // Brings a window forward, as the app in front lets a process do, and
  // says whether it is in front: a key goes to it only then.
  public static bool Front(long hwnd) {
    IntPtr window = new IntPtr(hwnd);
    uint ignored;
    uint front = GetWindowThreadProcessId(GetForegroundWindow(), out ignored);
    uint mine = GetCurrentThreadId();
    if (front != mine) AttachThreadInput(mine, front, true);
    BringWindowToTop(window);
    SetForegroundWindow(window);
    if (front != mine) AttachThreadInput(mine, front, false);
    System.Threading.Thread.Sleep(250);
    uint inFront; GetWindowThreadProcessId(GetForegroundWindow(), out inFront);
    uint wanted; GetWindowThreadProcessId(window, out wanted);
    return inFront == wanted;
  }
  public static bool InFront(int pid) {
    uint inFront; GetWindowThreadProcessId(GetForegroundWindow(), out inFront);
    return inFront == pid;
  }
  static IUIAutomation Uia() { return (IUIAutomation)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("ff48dba4-60ef-4201-aa87-54103eef594e"))); }
  static string Prop(IUIAutomationElement e, int id) {
    object v;
    if (e.GetCurrentPropertyValue(id, out v) != 0 || v == null || Marshal.IsComObject(v)) return "";
    return Convert.ToString(v);
  }
  // What a screen reader reads of an element: name, type, dialog, its full
  // description, toggle state, value, focus.
  static string Describe(IUIAutomationElement e) {
    StringBuilder s = new StringBuilder();
    s.Append("'" + Prop(e, 30005) + "' " + Prop(e, 30004));
    if (Prop(e, 30174) == "True") s.Append(" dialog");
    string description = Prop(e, 30159); if (description != "") s.Append(" description='" + description + "'");
    if (Prop(e, 30041) == "True") s.Append(" toggle=" + Prop(e, 30086));
    if (Prop(e, 30033) == "True") s.Append(" value=" + Prop(e, 30047) + " (min " + Prop(e, 30049) + ", max " + Prop(e, 30050) + ")");
    if (Prop(e, 30008) == "True") s.Append(" focused");
    return s.ToString();
  }
  static void Walk(IUIAutomationTreeWalker w, IUIAutomationElement e, int level, int depth, List<string> lines) {
    lines.Add(new string(' ', level * 2) + Describe(e));
    if (level >= depth) return;
    IUIAutomationElement child; w.GetFirstChildElement(e, out child);
    while (child != null) { Walk(w, child, level + 1, depth, lines); IUIAutomationElement next; w.GetNextSiblingElement(child, out next); child = next; }
  }
  // The first element under e whose name starts with prefix, or is it
  // exactly when prefix starts with "=".
  static IUIAutomationElement Find(IUIAutomationTreeWalker w, IUIAutomationElement e, string prefix, int level) {
    string name = level > 0 ? Prop(e, 30005) : null;
    if (name != null && (prefix.StartsWith("=") ? name == prefix.Substring(1) : name.StartsWith(prefix, StringComparison.Ordinal))) return e;
    if (level >= 30) return null;
    IUIAutomationElement child; w.GetFirstChildElement(e, out child);
    while (child != null) { IUIAutomationElement found = Find(w, child, prefix, level + 1); if (found != null) return found; IUIAutomationElement next; w.GetNextSiblingElement(child, out next); child = next; }
    return null;
  }
  // A window's element and its descendants to depth.
  public static string Read(long hwnd, int depth) {
    List<string> lines = new List<string>();
    IUIAutomation uia = Uia(); IUIAutomationElement root; uia.ElementFromHandle(new IntPtr(hwnd), out root);
    IUIAutomationTreeWalker walker; uia.get_RawViewWalker(out walker);
    if (root == null) return "no element";
    Walk(walker, root, 0, depth, lines);
    return string.Join("\n", lines);
  }
  // The element at the end of a path of name prefixes ("Pane, >More
  // options"), each looked for within the one before; "=Search" is a name
  // whole.
  static IUIAutomationElement At(long hwnd, string path, out string missing) {
    IUIAutomation uia = Uia(); IUIAutomationElement e; uia.ElementFromHandle(new IntPtr(hwnd), out e);
    IUIAutomationTreeWalker walker; uia.get_RawViewWalker(out walker);
    missing = "";
    foreach (string prefix in path.Split('>')) {
      e = e == null ? null : Find(walker, e, prefix, 0);
      if (e == null) { missing = prefix; return null; }
    }
    return e;
  }
  // Types into the field at the end of a path through its Value pattern, as
  // a screen reader's user's typing sets it.
  public static string Fill(long hwnd, string path, string text) {
    string missing; IUIAutomationElement e = At(hwnd, path, out missing);
    if (e == null) return "not found: " + missing;
    object p;
    if (e.GetCurrentPattern(10002, out p) == 0 && p != null) {
      int done = ((IUIAutomationValuePattern)p).SetValue(text);
      return done == 0 ? "typed into '" + Prop(e, 30005) + "'" : "SetValue: " + done;
    }
    return "no value pattern on '" + Prop(e, 30005) + "'";
  }
  // Presses, ticks, or selects the element at the end of a path through its
  // own pattern, as a screen reader's user does.
  public static string Act(long hwnd, string path) {
    string missing; IUIAutomationElement e = At(hwnd, path, out missing);
    if (e == null) return "not found: " + missing;
    string name = Prop(e, 30005);
    object p;
    if (e.GetCurrentPattern(10000, out p) == 0 && p != null) { ((IUIAutomationInvokePattern)p).Invoke(); return "invoked '" + name + "'"; }
    if (e.GetCurrentPattern(10015, out p) == 0 && p != null) { ((IUIAutomationTogglePattern)p).Toggle(); return "toggled '" + name + "'"; }
    if (e.GetCurrentPattern(10010, out p) == 0 && p != null) { ((IUIAutomationSelectionItemPattern)p).Select(); return "selected '" + name + "'"; }
    if (e.GetCurrentPattern(10005, out p) == 0 && p != null) { ((IUIAutomationExpandCollapsePattern)p).Expand(); return "expanded '" + name + "'"; }
    return "no pattern on '" + name + "'";
  }
}`;

// Each PowerShell's start: the helpers, compiled once into the run's
// temporary folder (loadHelpers), and the screen's capture. Compiling them
// on each command line would pass Windows' 32,767 characters.
let helpers = "";

const prelude = () => `
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -Path '${helpers}'
[W7b]::SetProcessDPIAware() | Out-Null
function Shot([string]$file, $rect) {
  $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  if ($rect) { $bounds = New-Object System.Drawing.Rectangle($rect.Left, $rect.Top, ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top)) }
  $bitmap = New-Object System.Drawing.Bitmap($bounds.Width, $bounds.Height)
  $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
  $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
  $bitmap.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
  $graphics.Dispose(); $bitmap.Dispose()
}
`;

async function loadHelpers(dir: string) {
	const source = join(dir, "W7b.cs");
	writeFileSync(source, HELPERS);
	helpers = join(dir, "W7b.dll");

	await powershell(
		"Add-Type -TypeDefinition (Get-Content -Raw -LiteralPath $env:SOURCE) -OutputAssembly $env:DLL -OutputType Library",
		{ SOURCE: source, DLL: helpers },
	);
}

const ps = (script: string, env: Record<string, string> = {}) =>
	powershell(`${prelude()}\n${script}`, env);

// The whole screen, or a window's frame, into `name`.png in the run's folder.
async function shot(out: string, name: string, hwnd?: string) {
	await ps(
		"$rect = $null; if ($env:HWND) { $rect = [W7b]::Frame([long]$env:HWND) }; Shot $env:FILE $rect",
		{ FILE: join(out, `${name}.png`), HWND: hwnd ?? "" },
	).catch((error) => console.warn(`capture ${name}: ${error}`));
}

type Window = { hwnd: string; cls: string; title: string };

async function windowsOf(pid: number): Promise<Window[]> {
	const text = await ps('[W7b]::Windows([int]$env:PID) -join "`n"', {
		PID: String(pid),
	});

	return text
		.split(/\r?\n/)
		.filter(Boolean)
		.map((line) => {
			const [hwnd = "", cls = "", title = ""] = line.split("|");

			return { hwnd, cls, title };
		});
}

async function until<T>(
	what: string,
	check: () => Promise<T | null | undefined | false>,
	timeoutMs: number,
	everyMs = 100,
): Promise<T> {
	const deadline = Date.now() + timeoutMs;

	for (;;) {
		const value = await check();

		if (value) {
			return value;
		}

		expect(Date.now() < deadline, `${what}: not in ${timeoutMs / 1000} s`);
		await Bun.sleep(everyMs);
	}
}

// The processes running an executable of `folder`.
async function processesFrom(folder: string): Promise<number[]> {
	const text = await powershell(
		"Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($env:FOLDER, [StringComparison]::OrdinalIgnoreCase) } | ForEach-Object { $_.ProcessId }",
		{ FOLDER: `${folder}\\` },
	);

	return text.split(/\r?\n/).filter(Boolean).map(Number);
}

// The pid of a process by its executable's name that shows a window, or 0.
async function pidOf(name: string): Promise<number> {
	return Number(
		(
			await powershell(
				"$p = Get-Process -Name $env:NAME -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1; if ($p) { $p.Id } else { 0 }",
				{ NAME: name },
			)
		).trim() || "0",
	);
}

// Presses what `path` names ("Pane>More options") in a window of `pid`,
// through UI Automation, as a screen reader's user presses it.
async function press(pid: number, path: string): Promise<string> {
	for (const window of await windowsOf(pid)) {
		const done = (
			await ps("[W7b]::Act([long]$env:HWND, $env:WHAT)", {
				HWND: window.hwnd,
				WHAT: path,
			})
		).trim();

		if (!done.startsWith("not found")) {
			return done;
		}
	}

	return `not found: ${path}`;
}

// A visible top-level window by its title.
async function windowTitled(title: string): Promise<Window | null> {
	const [hwnd = "", cls = "", name = ""] = (
		await ps("[W7b]::Titled($env:TITLE)", { TITLE: title })
	)
		.trim()
		.split("|");

	return hwnd ? { hwnd, cls, title: name } : null;
}

// Presses, or scrolls into view, what `path` names in `window`.
async function pressIn(window: Window, path: string): Promise<string> {
	return (
		await ps("[W7b]::Act([long]$env:HWND, $env:WHAT)", {
			HWND: window.hwnd,
			WHAT: path,
		})
	).trim();
}

async function typeIn(
	window: Window,
	path: string,
	text: string,
): Promise<string> {
	return (
		await ps("[W7b]::Fill([long]$env:HWND, $env:WHAT, $env:TEXT)", {
			HWND: window.hwnd,
			WHAT: path,
			TEXT: text,
		})
	).trim();
}

// Searches a Settings page's list for `text`: Default apps' box is an edit
// named "Search apps", Installed apps' one named "Search", its placeholder
// "Search apps".
async function searchFor(window: Window, text: string): Promise<string> {
	for (const box of ["=Search apps", "=Search"]) {
		const done = await typeIn(window, box, text);

		if (done.startsWith("typed")) {
			return done;
		}
	}

	return "no search box";
}

// Settings, once its window shows a page.
const settingsWindow = () =>
	until("Settings' window", async () => windowTitled("Settings"), 30_000, 250);

// What a screen reader reads of a window of `pid` and its controls.
async function heard(pid: number, depth = 1): Promise<string> {
	const [window] = await windowsOf(pid);

	return window
		? (
				await ps("[W7b]::Read([long]$env:HWND, [int]$env:DEPTH)", {
					HWND: window.hwnd,
					DEPTH: String(depth),
				})
			).trim()
		: "no window";
}

// Keys (SendKeys' syntax) to a window of `pid`, once it is proven in front.
async function keys(pid: number, text: string) {
	const [window] = await windowsOf(pid);
	expect(window, `no window of ${pid} to type into`);

	const sent = (
		await ps(
			"if ([W7b]::Front([long]$env:HWND) -and [W7b]::InFront([int]$env:PID)) { [System.Windows.Forms.SendKeys]::SendWait($env:KEYS); 'sent' } else { 'not in front' }",
			{ HWND: window.hwnd, PID: String(pid), KEYS: text },
		)
	).trim();

	expect(sent === "sent", `${text} to ${pid}: ${sent}`);
}

async function registry(key: string): Promise<Record<string, unknown> | null> {
	const text = await powershell(
		"if (Test-Path $env:KEY) { Get-ItemProperty -Path $env:KEY | ConvertTo-Json -Compress } else { 'null' }",
		{ KEY: key },
	);

	return JSON.parse(text.trim() || "null");
}

async function shortcut(path: string) {
	if (!existsSync(path)) {
		return null;
	}

	const text = await ps(
		"$s = (New-Object -ComObject WScript.Shell).CreateShortcut($env:LINK); [pscustomobject]@{ target = $s.TargetPath; icon = $s.IconLocation; description = $s.Description; appId = [W7b]::LinkAppId($env:LINK); toast = [W7b]::LinkToastActivator($env:LINK) } | ConvertTo-Json -Compress",
		{ LINK: path },
	);

	return JSON.parse(text.trim()) as {
		target: string;
		icon: string;
		description: string;
		appId: string;
		toast: string;
	};
}

// Windows' light or dark, as Settings' Personalization sets it.
async function windowsTheme(light: boolean) {
	await powershell(
		String.raw`$k = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize'; Set-ItemProperty $k AppsUseLightTheme ([int]$env:LIGHT); Set-ItemProperty $k SystemUsesLightTheme ([int]$env:LIGHT); Add-Type -Namespace W7bTheme -Name Broadcast -MemberDefinition '[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessageTimeout(IntPtr h, uint m, UIntPtr w, string l, uint f, uint t, out UIntPtr r);'; $r = [UIntPtr]::Zero; [W7bTheme.Broadcast]::SendMessageTimeout([IntPtr]0xffff, 0x1A, [UIntPtr]::Zero, 'ImmersiveColorSet', 2, 5000, [ref]$r) | Out-Null`,
		{ LIGHT: light ? "1" : "0" },
	);
}

// Everything of Pane's an uninstall must leave nowhere, by place.
async function leftovers() {
	return JSON.parse(
		(
			await powershell(
				String.raw`
$found = @()
foreach ($path in @("$env:LOCALAPPDATA\Pane", "$env:LOCALAPPDATA\Programs\Pane", "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Pane.lnk", "$env:USERPROFILE\Desktop\Pane.lnk", "$env:APPDATA\Microsoft\Internet Explorer\Quick Launch\User Pinned\TaskBar\Pane.lnk")) { if (Test-Path -LiteralPath $path) { $found += $path } }
foreach ($key in @('HKCU:\Software\Pane', 'HKCU:\Software\Classes\PaneHTM', 'HKCU:\Software\Classes\PanePDF', 'HKCU:\Software\Clients\StartMenuInternet\Pane', 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Pane', 'HKCU:\Software\Classes\CLSID\{643681EE-CBD6-4AA1-BFCF-837FC0A172E0}')) { if (Test-Path $key) { $found += $key } }
$registered = (Get-ItemProperty 'HKCU:\Software\RegisteredApplications' -ErrorAction SilentlyContinue).Pane
if ($registered) { $found += 'RegisteredApplications\Pane' }
Get-ChildItem $env:TEMP -Directory -Filter 'pane-uninstall-*' -ErrorAction SilentlyContinue | ForEach-Object { $found += $_.FullName }
Get-ChildItem 'HKCU:\Software\Classes' -ErrorAction SilentlyContinue | Where-Object { $_.PSChildName -like '.*' } | ForEach-Object { $p = Join-Path $_.PSPath 'OpenWithProgids'; if (Test-Path $p) { $v = Get-ItemProperty $p; if ($v.PSObject.Properties.Name -contains 'PaneHTM' -or $v.PSObject.Properties.Name -contains 'PanePDF') { $found += "$($_.PSChildName)\OpenWithProgids" } } }
ConvertTo-Json -Compress @($found)`,
			)
		).trim() || "[]",
	) as string[];
}

const windowsInstall: Scenario = async (context) => {
	expect(
		await inTheTestVM(),
		"the install runs in the test VM alone, for its own user: not on this computer",
	);

	await loadHelpers(context.tempDir("w7b-helpers"));

	const local = process.env.LOCALAPPDATA ?? "";
	const roaming = process.env.APPDATA ?? "";
	const profile = process.env.USERPROFILE ?? "";
	const app = join(local, "Programs", "Pane", "Application");
	const chrome = join(app, "chrome.exe");
	const data = join(local, "Pane");

	const startLink = join(
		roaming,
		"Microsoft",
		"Windows",
		"Start Menu",
		"Programs",
		"Pane.lnk",
	);

	const desktopLink = join(profile, "Desktop", "Pane.lnk");

	const pinned = join(
		roaming,
		"Microsoft",
		"Internet Explorer",
		"Quick Launch",
		"User Pinned",
		"TaskBar",
		"Pane.lnk",
	);

	const uninstallKey =
		"HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Pane";

	expect(
		local && !existsSync(app) && !existsSync(data),
		`Pane is installed or its data is here already (${app}, ${data}): this check makes and removes its own`,
	);

	const found: Record<string, unknown> = {};

	const record = () =>
		writeFileSync(
			join(context.out, "windows-install-found.json"),
			`${JSON.stringify(found, null, "\t")}\n`,
		);

	// The installer as a browser saves it: in Downloads, with its Mark of the
	// Web, as Edge and Chrome mark an exe from the web.
	const downloads = join(profile, "Downloads");
	const downloaded = join(downloads, context.binary.split("\\").pop() ?? "");
	copyFileSync(context.binary, downloaded);

	await powershell(
		'Set-Content -LiteralPath $env:FILE -Stream Zone.Identifier -Value "[ZoneTransfer]`r`nZoneId=3`r`nHostUrl=https://github.com/andredezzy/pane/releases/download/test/Pane-x64.exe"',
		{ FILE: downloaded },
	);

	// Pane's browser process and its window, once it shows one.
	const paneWindow = (what: string) =>
		until(
			what,
			async () => {
				for (const pid of await processesFrom(app)) {
					const [window] = (await windowsOf(pid)).filter((each) =>
						each.cls.startsWith("Chrome_WidgetWin"),
					);

					if (window) {
						return { pid, window };
					}
				}

				return null;
			},
			60_000,
			250,
		);

	// Pane's Exit Pane? asked and answered as Alt+F4 asks and answers it (ADR
	// 0053), then every process from the app folder gone.
	const exitPane = async (pid: number) => {
		await keys(pid, "%{F4}");
		await Bun.sleep(800);
		await keys(pid, "%{F4}");

		await until(
			"Pane's exit",
			async () => (await processesFrom(app)).length === 0,
			60_000,
			500,
		);
	};

	// The installer started as Explorer starts a file: its window, and what a
	// screen reader reads of it.
	const install = async (path: string, label: string) => {
		const pid = Number(
			(
				await powershell("(Start-Process -FilePath $env:FILE -PassThru).Id", {
					FILE: path,
				})
			).trim(),
		);

		const window = await until(
			`${label}: the install window`,
			async () => (await windowsOf(pid))[0],
			120_000,
			20,
		);

		const shown = Date.now();
		found[`${label}.window`] = { ...window, heard: await heard(pid) };

		return { pid, window, shown };
	};

	// What the installer wrote beside itself, if it failed.
	const installerLog = async (path: string) => {
		const log = path.replace(/\.exe$/i, ".log");

		return existsSync(log) ? await Bun.file(log).text() : "";
	};

	// The uninstall's dialog, shown by a copy of the helper outside the app
	// folder.
	const uninstallDialog = () =>
		until(
			"Pane's uninstall dialog",
			async () => {
				const pid = await pidOf("pane_setup");
				const [window] = pid ? await windowsOf(pid) : [];

				return window ? { pid, window } : null;
			},
			60_000,
			250,
		);

	// A dialog's screen, by the title it reads as.
	const screenOf = (pid: number, title: string, timeoutMs: number) =>
		until(
			`"${title}"`,
			async () => {
				const text = await heard(pid, 0);

				return text.startsWith(`'${title}'`) ? text : null;
			},
			timeoutMs,
			250,
		);

	try {
		// 1. The first install as Explorer opens a download: SmartScreen asks
		// first, for a file from the web (More info, then Run anyway; 11.12.17).
		// Explorer starts it, so nothing here waits inside the shell while
		// SmartScreen asks.
		const installerName = (context.binary.split("\\").pop() ?? "").replace(
			/\.exe$/i,
			"",
		);

		const startedAt = Date.now();

		await powershell(
			"Start-Process explorer.exe -ArgumentList ('\"' + $env:FILE + '\"')",
			{ FILE: downloaded },
		);

		const first = await until(
			"SmartScreen or the install window",
			async () => {
				const smartscreen = await pidOf("smartscreen");

				if (smartscreen) {
					return { smartscreen };
				}

				const installer = await pidOf(installerName);

				return installer ? { installer } : null;
			},
			60_000,
			250,
		);

		if ("smartscreen" in first && first.smartscreen) {
			await Bun.sleep(1_000);
			found.smartscreenHeard = await heard(first.smartscreen, 6);
			await shot(context.out, "smartscreen-1");
			found.moreInfo = await press(first.smartscreen, "More info");
			await Bun.sleep(1_000);
			await shot(context.out, "smartscreen-2");
			found.runAnyway = await press(first.smartscreen, "Run anyway");
		} else {
			found.smartscreen = "not shown";
		}

		const shell = await until(
			"the installer's process",
			async () => pidOf(installerName),
			120_000,
			50,
		);

		const installWindow = await until(
			"the install window",
			async () => (await windowsOf(shell))[0],
			120_000,
			20,
		);

		const shownAt = Date.now();

		found.firstInstall = {
			window: installWindow,
			fromStart: shownAt - startedAt,
			heard: await heard(shell, 2),
		};

		// Its states as they go: copying, adding, opening.
		for (let frame = 0; (await windowsOf(shell)).length > 0; frame++) {
			expect(frame < 600, "the first install: still open after 600 frames");

			if (frame % 3 === 0) {
				await shot(
					context.out,
					`install-${String(frame / 3).padStart(3, "0")}`,
					installWindow.hwnd,
				);
			}

			await Bun.sleep(100);
		}

		const closedAt = Date.now();
		const failedFirst = await installerLog(downloaded);

		found.firstInstall = {
			...(found.firstInstall as object),
			// From the install window's first frame to its close, as Pane's
			// first window paints (11.12.6).
			ms: closedAt - shownAt,
			installerLog: failedFirst,
		};

		record();
		expect(failedFirst === "", `the first install failed: ${failedFirst}`);
		const pane = await paneWindow("Pane's window after the install");
		await Bun.sleep(2_000);
		await shot(context.out, "pane-after-install");

		const appIdOfWindow = (
			await ps("[W7b]::WindowAppId([long]$env:HWND)", {
				HWND: pane.window.hwnd,
			})
		).trim();

		found.paneWindow = { ...pane, appId: appIdOfWindow };

		// 2. Start and the desktop: Pane's two shortcuts, its icon, its
		// AppUserModelID (the window's, so the taskbar groups them), and the
		// toast activator.
		const start = await shortcut(startLink);
		const desktop = await shortcut(desktopLink);
		found.shortcuts = { start, desktop };
		record();

		for (const [name, link] of [
			["Start's", start],
			["the desktop's", desktop],
		] as const) {
			expect(link, `${name} shortcut is missing`);

			expect(
				link.target.toLowerCase() === chrome.toLowerCase(),
				`${name} shortcut starts ${link.target}`,
			);

			expect(
				/\\AppIcon-Day\.ico,0$/i.test(link.icon),
				`${name} shortcut's icon, Automatic's Day in Windows' light: ${link.icon}`,
			);

			expect(
				link.appId !== "" && link.appId === appIdOfWindow,
				`${name} shortcut's AppUserModelID ${link.appId}, the window's ${appIdOfWindow}`,
			);

			expect(
				link.description === "Every profile, its own internet.",
				`${name} shortcut's tooltip: ${link.description}`,
			);
		}

		expect(
			start?.toast === TOAST_ACTIVATOR,
			`Start's shortcut's toast activator: ${start?.toast}`,
		);

		found.startApps = (
			await powershell(
				"Get-StartApps | Where-Object { $_.Name -eq 'Pane' } | ConvertTo-Json -Compress",
			)
		).trim();

		expect(
			/"AppID"\s*:\s*"[^"]+"/.test(String(found.startApps)),
			`Start's All apps lists no Pane: ${found.startApps}`,
		);

		// 3. Default apps and Installed apps, as Windows reads them.
		const entry = await registry(uninstallKey);

		const capabilities = await registry(
			"HKCU:\\Software\\Clients\\StartMenuInternet\\Pane\\Capabilities",
		);

		const activator = await registry(
			`HKCU:\\Software\\Classes\\CLSID\\${TOAST_ACTIVATOR}\\LocalServer32`,
		);

		const appPaths = await registry(
			"HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe",
		);

		Object.assign(found, { entry, capabilities, activator, appPaths });
		record();

		expect(
			entry?.DisplayName === "Pane" &&
				entry.Publisher === "Rounded Corners" &&
				typeof entry.DisplayVersion === "string" &&
				/^\d+\.\d+\.\d+$/.test(entry.DisplayVersion) &&
				String(entry.UninstallString).endsWith("--pane-uninstall") &&
				String(entry.DisplayIcon).toLowerCase() ===
					join(app, "Pane.ico").toLowerCase() &&
				entry.NoModify === 1 &&
				entry.NoRepair === 1,
			`the uninstall entry: ${JSON.stringify(entry)}`,
		);

		expect(
			capabilities?.ApplicationName === "Pane" &&
				capabilities.ApplicationDescription ===
					"Every profile, its own internet: its own sign-ins, proxy, fingerprint, and extensions.",
			`Default apps' entry: ${JSON.stringify(capabilities)}`,
		);

		expect(
			String(activator?.ServerExecutable).toLowerCase() ===
				join(app, "notification_helper.exe").toLowerCase(),
			`the toast activator: ${JSON.stringify(activator)}`,
		);

		expect(appPaths === null, "App Paths chrome.exe was written");

		// Each page searched for Pane, its row alone in the list.
		for (const [page, name] of [
			["ms-settings:defaultapps", "settings-default-apps"],
			["ms-settings:appsfeatures", "settings-installed-apps"],
		] as const) {
			await powershell("Start-Process $env:PAGE", { PAGE: page });
			await Bun.sleep(4_000);
			found[name] = await searchFor(await settingsWindow(), "Pane");
			await Bun.sleep(2_000);
			await shot(context.out, name);
		}

		await powershell(
			"Get-Process SystemSettings -ErrorAction SilentlyContinue | Stop-Process -Force",
		);

		// 3b. Pane's first profile, made by a page handed to the running Pane
		// as another app hands one: Pane's own shortcuts stay as the install
		// made them. Chrome's profile shortcuts (ProfileShortcutManagerWin),
		// which run where the engine's folder is Chromium's default, as the
		// install's is, turned the desktop's Pane.lnk into "Your Chromium -
		// Chromium" for the Default profile once a second profile appeared (W8).
		const page = join(context.tempDir("w8-page"), "w8-page.html");
		writeFileSync(page, "<!doctype html><title>W8 page</title>");
		Bun.spawn([chrome, page], { cwd: profile });

		found.firstProfile = await until(
			"Pane's first profile",
			async () => {
				const engine = join(data, "User Data");

				return existsSync(engine)
					? readdirSync(engine).find((name) =>
							/^[0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12}$/i.test(name),
						)
					: undefined;
			},
			60_000,
			500,
		);

		await Bun.sleep(5_000);

		const profileShortcuts = (
			await powershell(
				"$shell = New-Object -ComObject WScript.Shell; Get-ChildItem ([Environment]::GetFolderPath('Desktop')) -Filter *.lnk -ErrorAction SilentlyContinue | ForEach-Object { $s = $shell.CreateShortcut($_.FullName); if ($s.TargetPath -ieq $env:PANE_CHROME) { \"$($_.Name)|$($s.Arguments)\" } }",
				{ PANE_CHROME: chrome },
			)
		)
			.split(/\r?\n/)
			.filter(Boolean);

		found.desktopAfterAProfile = profileShortcuts;
		record();

		expect(
			profileShortcuts.length === 1 && profileShortcuts[0] === "Pane.lnk|",
			`the desktop's shortcuts to Pane once it has a profile: ${JSON.stringify(profileShortcuts)}`,
		);

		// 4. A reinstall over the open Pane: "Pane is open", Exit Pane asking
		// Pane's Exit Pane?, then the install going on by itself. The desktop's
		// shortcut, deleted, stays deleted.
		rmSync(desktopLink, { force: true });
		const second = await install(context.binary, "reinstall");
		await Bun.sleep(800);
		await shot(context.out, "install-pane-is-open", second.window.hwnd);
		found.exitPane = await press(second.pid, "Exit Pane");
		await Bun.sleep(1_500);
		await shot(context.out, "pane-exit-asked");
		await keys(pane.pid, "%{F4}");

		await until(
			"the reinstall's end",
			async () => (await windowsOf(second.pid)).length === 0,
			180_000,
			250,
		);

		found.reinstall = {
			ms: Date.now() - second.shown,
			start: existsSync(startLink),
			desktop: existsSync(desktopLink),
			installerLog: await installerLog(context.binary),
		};

		record();
		expect(existsSync(startLink), "the reinstall left no Start shortcut");

		expect(
			!existsSync(desktopLink),
			"the reinstall put back the desktop's shortcut the user deleted",
		);

		const reopened = await paneWindow("Pane after the reinstall");

		// 5. The App icon's sky reaching the shortcuts: Automatic follows
		// Windows' dark while Pane runs, and a setting made while Pane was
		// closed reaches them at its next launch, a pin with them.
		await windowsTheme(false);

		found.darkChange = await until(
			"Start's shortcut naming Night once Windows turned dark",
			async () => {
				const link = await shortcut(startLink);

				return link && /AppIcon-Night\.ico/i.test(link.icon) ? link : null;
			},
			30_000,
			500,
		);

		await windowsTheme(true);

		await until(
			"Start's shortcut naming Day again",
			async () => {
				const link = await shortcut(startLink);

				return link && /AppIcon-Day\.ico/i.test(link.icon);
			},
			30_000,
			500,
		);

		await exitPane(reopened.pid);
		// settings.json as Pane keeps it: {"state":{"settings":{…}},"version":0}.
		const settingsFile = join(data, "settings.json");

		const saved = existsSync(settingsFile)
			? JSON.parse(await Bun.file(settingsFile).text())
			: { state: { settings: {} }, version: 0 };

		saved.state.settings = { ...saved.state.settings, appIcon: "sunset" };
		writeFileSync(settingsFile, JSON.stringify(saved));

		await powershell(
			"$s = (New-Object -ComObject WScript.Shell).CreateShortcut($env:LINK); $s.TargetPath = $env:TARGET; $s.IconLocation = $env:ICON; $s.Save()",
			{ LINK: pinned, TARGET: chrome, ICON: `${join(app, "Pane.ico")},0` },
		);

		Bun.spawn([chrome], { cwd: profile });

		found.sunsetAtLaunch = await until(
			"the shortcuts and the pin naming Sunset after the launch",
			async () => {
				const links = await Promise.all([startLink, pinned].map(shortcut));

				return links.every(
					(link) => link && /AppIcon-Sunset\.ico/i.test(link.icon),
				)
					? links
					: null;
			},
			60_000,
			500,
		);

		const sunset = await paneWindow("Pane on Sunset");
		await Bun.sleep(1_500);
		await shot(context.out, "pane-sunset");

		// 6. A PIN, set up in Settings as its user sets one up: Ctrl+, then Set
		// up PIN, the digits and Enter, the digits again.
		await keys(sunset.pid, "^,");
		await Bun.sleep(1_500);
		found.setUpPin = await press(sunset.pid, "Set up PIN");
		await Bun.sleep(1_000);
		await keys(sunset.pid, `${PIN}{ENTER}`);
		await Bun.sleep(1_000);
		await keys(sunset.pid, PIN);
		await Bun.sleep(1_500);
		await shot(context.out, "pane-pin-set");
		const security = join(data, "security.json");

		found.pinSet = await until(
			"the PIN kept in security.json",
			async () =>
				existsSync(security) &&
				/"length"\s*:\s*4/.test(await Bun.file(security).text()),
			10_000,
			250,
		);

		await exitPane(sunset.pid);

		// 7. The uninstall from Installed apps, its user's way: Pane's More
		// options, Uninstall, Windows' own question (11.12.4), then Pane's
		// dialog (11.12.9), its data kept by default.
		await powershell("Start-Process ms-settings:appsfeatures");
		await Bun.sleep(4_000);
		const settings = await settingsWindow();

		found.paneRow = await until(
			"Installed apps searched for Pane",
			async () => {
				const done = await searchFor(settings, "Pane");

				return done.startsWith("typed") ? done : null;
			},
			20_000,
			1_000,
		);

		await Bun.sleep(2_000);
		await shot(context.out, "settings-installed-apps-before-uninstall");

		await Bun.sleep(1_000);
		found.moreOptions = await pressIn(settings, "Pane, >More options");
		await Bun.sleep(1_000);
		await shot(context.out, "settings-pane-menu");
		found.uninstallItem = await pressIn(settings, "Uninstall");
		await Bun.sleep(1_500);

		found.windowsQuestion = (
			await ps("[W7b]::Read([long]$env:HWND, 6)", { HWND: settings.hwnd })
		)
			.split(/\r?\n/)
			.filter((line) => /Popup|uninstalled|'Uninstall'/.test(line))
			.join("\n");

		await shot(context.out, "settings-uninstall-question");
		// The question's own Uninstall: the menu has gone, so the first of the
		// name.
		found.uninstallConfirm = await pressIn(settings, "Uninstall");

		const kept = await uninstallDialog();
		await Bun.sleep(1_000);
		found.uninstallAsking = await heard(kept.pid, 2);
		await shot(context.out, "uninstall-asking", kept.window.hwnd);
		found.uninstallKeep = await press(kept.pid, "Uninstall");

		found.uninstallKept = await screenOf(
			kept.pid,
			"Pane is uninstalled",
			120_000,
		);

		await shot(context.out, "uninstall-done-kept", kept.window.hwnd);

		expect(
			String(found.uninstallKept).includes(
				"description='Your data stays in %LOCALAPPDATA%\\Pane",
			),
			`the uninstall keeping the data says where it stays: ${found.uninstallKept}`,
		);

		await press(kept.pid, "Close");

		await powershell(
			"Get-Process SystemSettings -ErrorAction SilentlyContinue | Stop-Process -Force",
		);

		const afterKeep = await until(
			"the helper's copy gone, the data kept",
			async () => {
				const left = await leftovers();

				return left.length === 2 ? left : null;
			},
			30_000,
			500,
		);

		found.afterKeep = afterKeep;
		record();

		expect(
			JSON.stringify(afterKeep.map((each) => each.toLowerCase()).sort()) ===
				JSON.stringify(
					[data, "HKCU:\\Software\\Pane"]
						.map((each) => each.toLowerCase())
						.sort(),
				),
			`after the uninstall keeping the data, only the data and its MACs stay: ${JSON.stringify(afterKeep)}`,
		);

		// 8. A reinstall finds the data: its shortcuts take its App icon,
		// Sunset, and Pane opens locked behind its PIN.
		const third = await install(context.binary, "reinstall-after-keep");

		await until(
			"the reinstall after the uninstall",
			async () => (await windowsOf(third.pid)).length === 0,
			180_000,
			250,
		);

		found.reinstallAfterKeep = {
			ms: Date.now() - third.shown,
			installerLog: await installerLog(context.binary),
			start: await shortcut(startLink),
		};

		record();

		expect(
			/AppIcon-Sunset\.ico/i.test((await shortcut(startLink))?.icon ?? ""),
			"the reinstall's Start shortcut doesn't take the kept data's App icon",
		);

		const locked = await paneWindow("Pane, locked, after the reinstall");
		await Bun.sleep(2_000);
		await shot(context.out, "pane-locked-after-reinstall");
		await keys(locked.pid, PIN);
		await Bun.sleep(2_000);
		await shot(context.out, "pane-unlocked-after-reinstall");
		await exitPane(locked.pid);

		// 9. An installer older than the Pane installed: refused, nothing
		// changed (11.12.7).
		const versionFile = join(app, "pane-version");
		const installedVersion = await Bun.file(versionFile).text();
		writeFileSync(versionFile, "99.0.0");
		const older = await install(context.binary, "older");
		await Bun.sleep(1_000);
		found.newer = await heard(older.pid, 2);
		await shot(context.out, "install-newer", older.window.hwnd);

		expect(
			String(found.newer).startsWith("'A newer Pane is installed'"),
			`over a newer Pane: ${found.newer}`,
		);

		await press(older.pid, "Close");
		writeFileSync(versionFile, installedVersion);

		// 10. The uninstall erasing the data: the box ticked through UI
		// Automation's Toggle, Uninstall and delete, then nothing of Pane's left
		// (11.12.9, 11.12.10).
		const uninstallString = String(
			(await registry(uninstallKey))?.UninstallString,
		);

		await powershell(
			"$exe, $rest = $env:COMMAND -split '\" ', 2; Start-Process -FilePath $exe.Trim('\"') -ArgumentList $rest",
			{ COMMAND: uninstallString },
		);

		const erase = await uninstallDialog();
		await Bun.sleep(1_000);
		found.tick = await press(erase.pid, "Also delete");
		await Bun.sleep(800);
		found.uninstallTicked = await heard(erase.pid, 2);
		await shot(context.out, "uninstall-ticked", erase.window.hwnd);
		found.uninstallErase = await press(erase.pid, "Uninstall and delete");

		for (let frame = 0; frame < 12; frame++) {
			await shot(
				context.out,
				`uninstall-erasing-${String(frame).padStart(2, "0")}`,
				erase.window.hwnd,
			);

			await Bun.sleep(150);
		}

		found.uninstallErased = await screenOf(
			erase.pid,
			"Pane is uninstalled",
			180_000,
		);

		await shot(context.out, "uninstall-done-erased", erase.window.hwnd);

		expect(
			String(found.uninstallErased).includes(
				"description='Pane and its data are gone from this PC.'",
			),
			`the uninstall erasing the data says so: ${found.uninstallErased}`,
		);

		await press(erase.pid, "Close");

		found.afterErase = await until(
			"nothing of Pane's left",
			async () => {
				const left = await leftovers();

				return left.length === 0 ? left : null;
			},
			30_000,
			500,
		);

		// 11. An uninstall with no data (W8d): Pane installed again, its data
		// deleted once it has exited, as its user may delete the folder, then
		// the uninstall from its entry: no data box, a last screen that says
		// nothing of data (it said the data stayed), and nothing of Pane's
		// left, the data's preference MACs neither.
		const fourth = await install(context.binary, "no-data");

		await until(
			"the install for the uninstall with no data",
			async () => (await windowsOf(fourth.pid)).length === 0,
			180_000,
			250,
		);

		const fresh = await paneWindow("Pane, opened by its install");
		await Bun.sleep(2_000);
		await exitPane(fresh.pid);
		rmSync(data, { recursive: true, force: true });

		await powershell(
			"$exe, $rest = $env:COMMAND -split '\" ', 2; Start-Process -FilePath $exe.Trim('\"') -ArgumentList $rest",
			{ COMMAND: String((await registry(uninstallKey))?.UninstallString) },
		);

		const bare = await uninstallDialog();
		await Bun.sleep(1_000);
		found.uninstallNoData = await heard(bare.pid, 2);
		await shot(context.out, "uninstall-no-data", bare.window.hwnd);

		expect(
			!String(found.uninstallNoData).includes("Also delete"),
			`with no data, the uninstall offers no data to delete: ${found.uninstallNoData}`,
		);

		await press(bare.pid, "Uninstall");

		found.uninstallDoneNoData = await screenOf(
			bare.pid,
			"Pane is uninstalled",
			120_000,
		);

		await shot(context.out, "uninstall-done-no-data", bare.window.hwnd);

		expect(
			!/description=|data/i.test(String(found.uninstallDoneNoData)),
			`with no data, the last screen says nothing of data: ${found.uninstallDoneNoData}`,
		);

		await press(bare.pid, "Close");

		found.afterNoData = await until(
			"nothing of Pane's left after the uninstall with no data",
			async () => {
				const left = await leftovers();

				return left.length === 0 ? left : null;
			},
			30_000,
			500,
		);
	} catch (error) {
		found.error = String(error);

		throw error;
	} finally {
		record();

		// The download goes once nothing runs from it.
		for (let attempt = 0; attempt < 20; attempt++) {
			try {
				rmSync(downloaded, { force: true });
				rmSync(downloaded.replace(/\.exe$/i, ".log"), { force: true });

				break;
			} catch {
				await Bun.sleep(500);
			}
		}

		await windowsTheme(true).catch(() => {});
	}

	return found;
};

export default windowsInstall;
