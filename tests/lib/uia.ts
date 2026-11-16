// Windows' UI Automation, as a screen reader reads Pane (ADR 0071): what the
// Windows sides of windows.ts, computer-use.ts, and surfaces.ts read and press
// where the Mac's read and press its accessibility. It reads through UI
// Automation's own client (uiautomationcore through COM, as Narrator, NVDA,
// and JAWS read; the managed client reads Win32 controls as panes), in the
// control view, Narrator's order. Its C# is compiled once into a library
// beside the runner's other tools, and again once its source changes.
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { powershell } from "./platform";

const SOURCE = String.raw`using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
public static class PaneUia {
  [ComImport, Guid("30cbe57d-d9d0-452a-ab13-7ac5ac4825ee"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IUIAutomation {
    void CompareElements(); void CompareRuntimeIds(); void GetRootElement();
    [PreserveSig] int ElementFromHandle(IntPtr hwnd, out IUIAutomationElement element);
    void ElementFromPoint(); void GetFocusedElement(); void GetRootElementBuildCache(); void ElementFromHandleBuildCache();
    void ElementFromPointBuildCache(); void GetFocusedElementBuildCache(); void CreateTreeWalker();
    [PreserveSig] int get_ControlViewWalker(out IUIAutomationTreeWalker walker);
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
  [ComImport, Guid("619be086-1f4e-4ee4-bafa-210128738730"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  public interface IUIAutomationExpandCollapsePattern { [PreserveSig] int Expand(); }
  public delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr lParam);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr hwnd, StringBuilder text, int count);
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
  [DllImport("user32.dll")] static extern int GetWindowLong(IntPtr hwnd, int index);
  [DllImport("user32.dll")] static extern uint GetDpiForWindow(IntPtr hwnd);
  [DllImport("user32.dll")] static extern IntPtr MonitorFromWindow(IntPtr hwnd, uint flags);
  [DllImport("user32.dll")] static extern bool GetMonitorInfo(IntPtr monitor, ref MONITORINFO info);
  [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr hwnd, ref POINT point);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  public struct RECT { public int Left, Top, Right, Bottom; }
  public struct POINT { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)] public struct MONITORINFO { public int cbSize; public RECT rcMonitor; public RECT rcWork; public uint dwFlags; }

  static IUIAutomation Uia() { return (IUIAutomation)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("ff48dba4-60ef-4201-aa87-54103eef594e"))); }
  static object Raw(IUIAutomationElement e, int id) {
    object v;
    if (e.GetCurrentPropertyValue(id, out v) != 0 || v == null || Marshal.IsComObject(v)) return null;
    return v;
  }
  static string Prop(IUIAutomationElement e, int id) { object v = Raw(e, id); return v == null ? "" : Convert.ToString(v, CultureInfo.InvariantCulture); }
  static bool Flag(IUIAutomationElement e, int id) { object v = Raw(e, id); return v is bool && (bool)v; }
  static readonly Dictionary<int, string> Types = new Dictionary<int, string> {
    {50000,"Button"},{50002,"CheckBox"},{50003,"ComboBox"},{50004,"Edit"},{50005,"Hyperlink"},{50006,"Image"},
    {50007,"ListItem"},{50008,"List"},{50009,"Menu"},{50010,"MenuBar"},{50011,"MenuItem"},{50012,"ProgressBar"},{50013,"RadioButton"},
    {50014,"ScrollBar"},{50015,"Slider"},{50018,"Tab"},{50019,"TabItem"},{50020,"Text"},{50021,"ToolBar"},{50023,"Tree"},
    {50024,"TreeItem"},{50025,"Custom"},{50026,"Group"},{50027,"Thumb"},{50030,"Document"},{50032,"Window"},{50033,"Pane"},
    {50038,"Separator"} };
  static string Esc(string s) {
    var b = new StringBuilder();
    foreach (char c in s ?? "") {
      if (c == '"' || c == '\\') b.Append('\\').Append(c);
      else if (c < 0x20) b.AppendFormat("\\u{0:x4}", (int)c);
      else b.Append(c);
    }
    return b.ToString();
  }
  // An element as a screen reader reads it.
  static string Json(IUIAutomationElement e, int depth) {
    var p = new List<string>();
    Action<string, string> s = (k, v) => p.Add("\"" + k + "\":\"" + Esc(v) + "\"");
    object type = Raw(e, 30003);
    p.Add("\"depth\":" + depth);
    s("role", type is int && Types.ContainsKey((int)type) ? Types[(int)type] : Convert.ToString(type));
    s("subrole", Prop(e, 30004));
    s("title", Prop(e, 30005));
    s("description", Prop(e, 30159));
    string value = Flag(e, 30043) ? Prop(e, 30045) : Flag(e, 30033) ? Prop(e, 30047) : "";
    s("value", value);
    s("help", Prop(e, 30013));
    if (Flag(e, 30041)) s("toggle", new[] { "off", "on", "mixed" }[Math.Max(0, Math.Min(2, Convert.ToInt32(Raw(e, 30086) ?? 0)))]);
    if (Flag(e, 30036)) p.Add("\"selected\":" + (Flag(e, 30079) ? "true" : "false"));
    if (Flag(e, 30028)) s("expanded", new[] { "collapsed", "expanded", "partial", "leaf" }[Math.Max(0, Math.Min(3, Convert.ToInt32(Raw(e, 30070) ?? 0)))]);
    string heading = Prop(e, 30173);
    if (heading.StartsWith("8005") && heading != "80050") p.Add("\"headingLevel\":" + (Convert.ToInt32(heading) - 80050));
    string size = Prop(e, 30153);
    if (size != "" && size != "0" && size != "-1") s("position", Prop(e, 30152) + " of " + size);
    string level = Prop(e, 30154);
    if (level != "" && level != "0" && level != "-1") p.Add("\"level\":" + level);
    if (Flag(e, 30174)) p.Add("\"dialog\":true");
    if (Flag(e, 30019)) p.Add("\"protected\":true");
    if (Flag(e, 30008)) p.Add("\"focused\":true");
    if (!Flag(e, 30010)) p.Add("\"enabled\":false");
    double[] r = Raw(e, 30001) as double[];
    if (r != null && r.Length == 4) p.Add("\"rect\":[" + (int)r[0] + "," + (int)r[1] + "," + (int)r[2] + "," + (int)r[3] + "]");
    return "{" + string.Join(",", p) + "}";
  }
  static void Walk(IUIAutomationTreeWalker w, IUIAutomationElement e, int level, List<string> lines) {
    lines.Add(Json(e, level));
    if (level >= 80) return;
    IUIAutomationElement child; w.GetFirstChildElement(e, out child);
    while (child != null) { Walk(w, child, level + 1, lines); IUIAutomationElement next; w.GetNextSiblingElement(child, out next); child = next; }
  }
  // A window's element and its descendants in the control view, as JSON.
  public static string Tree(long hwnd) {
    IUIAutomation uia = Uia(); IUIAutomationElement root; uia.ElementFromHandle(new IntPtr(hwnd), out root);
    if (root == null) return "[]";
    IUIAutomationTreeWalker walker; uia.get_ControlViewWalker(out walker);
    var lines = new List<string>();
    Walk(walker, root, 0, lines);
    return "[" + string.Join(",", lines) + "]";
  }
  // Named so: the name alone, or a menu item's with its shortcut after it, as
  // Views names one ("Exit Pane Alt+F4", "Full Screen F11").
  static bool Named(IUIAutomationElement e, string name) {
    string actual = Prop(e, 30005);
    if (actual == name) return true;
    object type = Raw(e, 30003);
    return type is int && (int)type == 50011 && actual.StartsWith(name + " ") &&
      System.Text.RegularExpressions.Regex.IsMatch(actual.Substring(name.Length + 1), @"^(((Ctrl|Alt|Shift|Win)\+)+\S+|F\d{1,2})$");
  }
  // Whether a screen reader's user can press it: Invoke, or Expand.
  static bool Pressable(IUIAutomationElement e) {
    object pattern;
    return (e.GetCurrentPattern(10000, out pattern) == 0 && pattern != null) ||
      (e.GetCurrentPattern(10005, out pattern) == 0 && pattern != null);
  }
  // The first element so named that can be pressed: a window's region takes
  // its page's title, "Pane" with none.
  static IUIAutomationElement Find(IUIAutomationTreeWalker w, IUIAutomationElement e, string name, int level) {
    if (level > 0 && Named(e, name) && Pressable(e)) return e;
    if (level >= 80) return null;
    IUIAutomationElement child; w.GetFirstChildElement(e, out child);
    while (child != null) { IUIAutomationElement found = Find(w, child, name, level + 1); if (found != null) return found; IUIAutomationElement next; w.GetNextSiblingElement(child, out next); child = next; }
    return null;
  }
  // Presses the element named \`name\` in the window, as a screen reader's
  // user does: Invoke, else Expand; what it did, or why not.
  public static string Press(long hwnd, string name) {
    IUIAutomation uia = Uia(); IUIAutomationElement root; uia.ElementFromHandle(new IntPtr(hwnd), out root);
    if (root == null) return "no window";
    IUIAutomationTreeWalker walker; uia.get_ControlViewWalker(out walker);
    IUIAutomationElement e = Find(walker, root, name, 0);
    if (e == null) return "none named " + name;
    object pattern;
    if (e.GetCurrentPattern(10000, out pattern) == 0 && pattern != null) {
      return ((IUIAutomationInvokePattern)pattern).Invoke() == 0 ? "invoked" : "invoke refused";
    }
    if (e.GetCurrentPattern(10005, out pattern) == 0 && pattern != null) {
      return ((IUIAutomationExpandCollapsePattern)pattern).Expand() == 0 ? "expanded" : "expand refused";
    }
    return "no pattern on " + name;
  }
  static string Box(RECT r, double scale) {
    return "{\"X\":" + (int)Math.Round(r.Left / scale) + ",\"Y\":" + (int)Math.Round(r.Top / scale) +
      ",\"Width\":" + (int)Math.Round((r.Right - r.Left) / scale) + ",\"Height\":" + (int)Math.Round((r.Bottom - r.Top) / scale) + "}";
  }
  // The process's visible top-level windows, menus and popups among them,
  // in DIPs: handle, class, title, frame, whether a tool window (a menu),
  // and its screen's frame and work area.
  public static string Windows(uint pid) {
    var found = new List<string>();
    EnumWindows((hwnd, lp) => {
      uint owner; GetWindowThreadProcessId(hwnd, out owner);
      if (owner != pid || !IsWindowVisible(hwnd)) return true;
      var title = new StringBuilder(512); GetWindowText(hwnd, title, title.Capacity);
      var name = new StringBuilder(256); GetClassName(hwnd, name, name.Capacity);
      RECT rect; GetWindowRect(hwnd, out rect);
      uint dpi = GetDpiForWindow(hwnd);
      double scale = dpi == 0 ? 1 : dpi / 96.0;
      var info = new MONITORINFO(); info.cbSize = Marshal.SizeOf(info);
      GetMonitorInfo(MonitorFromWindow(hwnd, 2), ref info);
      var client = new POINT(); ClientToScreen(hwnd, ref client);
      found.Add("{\"id\":" + hwnd.ToInt64() + ",\"class\":\"" + Esc(name.ToString()) + "\",\"title\":\"" + Esc(title.ToString()) +
        "\",\"client\":{\"X\":" + (int)Math.Round(client.X / scale) + ",\"Y\":" + (int)Math.Round(client.Y / scale) + "}" +
        ",\"tool\":" + (((uint)GetWindowLong(hwnd, -20) & 0x80) != 0 ? "true" : "false") +
        ",\"bounds\":" + Box(rect, scale) + ",\"screen\":{\"frame\":" + Box(info.rcMonitor, scale) + ",\"visible\":" + Box(info.rcWork, scale) + "}}");
      return true;
    }, IntPtr.Zero);
    return "[" + string.Join(",", found) + "]";
  }
}`;

const LIBRARY = join(
	tmpdir(),
	"pane-tests",
	`pane-uia-${createHash("sha256").update(SOURCE).digest("hex").slice(0, 12)}.dll`,
);

let compiled: Promise<void> | undefined;

// The library, compiled the first time it is asked for in this runner (Windows
// PowerShell's Add-Type compiles C# 5).
function library(): Promise<void> {
	compiled ??= (async () => {
		if (existsSync(LIBRARY)) {
			return;
		}

		mkdirSync(join(tmpdir(), "pane-tests"), { recursive: true });
		const source = `${LIBRARY}.cs`;
		writeFileSync(source, SOURCE);

		await powershell(
			"Add-Type -TypeDefinition (Get-Content -Raw -LiteralPath $env:PANE_SOURCE) -OutputAssembly $env:PANE_LIBRARY -OutputType Library",
			{ PANE_SOURCE: source, PANE_LIBRARY: LIBRARY },
		);
	})();

	return compiled;
}

// What `script` prints, with PaneUia to call, its output UTF-8.
async function uia(
	script: string,
	env: Record<string, string> = {},
): Promise<string> {
	await library();

	return powershell(
		`Add-Type -Path $env:PANE_LIBRARY\n[void][PaneUia]::SetProcessDPIAware()\n[Console]::OutputEncoding = [Text.Encoding]::UTF8\n${script}`,
		{ ...env, PANE_LIBRARY: LIBRARY },
	);
}

// An element as UI Automation gives it to a screen reader: its control type
// (`role`), its localized type (`subrole`), its name (`title`), its full
// description, its value (Value's, else RangeValue's), and its states.
export type UiaElement = {
	depth: number;
	role: string;
	subrole: string;
	title: string;
	description: string;
	value: string;
	help: string;
	toggle?: "off" | "on" | "mixed";
	selected?: boolean;
	expanded?: "collapsed" | "expanded" | "partial" | "leaf";
	headingLevel?: number;
	position?: string;
	level?: number;
	dialog?: boolean;
	protected?: boolean;
	focused?: boolean;
	enabled?: false;
	rect?: [number, number, number, number];
};

// The window's tree in UI Automation's control view, Narrator's order.
export async function uiaTree(windowId: number): Promise<UiaElement[]> {
	return JSON.parse(
		await uia("[Console]::Write([PaneUia]::Tree([long]$env:PANE_WINDOW))", {
			PANE_WINDOW: String(windowId),
		}),
	) as UiaElement[];
}

// Presses the element named `name` in the window as a screen reader's user
// does (Invoke, else Expand): what it did.
export async function uiaPress(
	windowId: number,
	name: string,
): Promise<string> {
	return (
		await uia(
			"[Console]::Write([PaneUia]::Press([long]$env:PANE_WINDOW, $env:PANE_NAME))",
			{ PANE_WINDOW: String(windowId), PANE_NAME: name },
		)
	).trim();
}

type Box = { X: number; Y: number; Width: number; Height: number };

export type UiaWindow = {
	id: number;
	class: string;
	title: string;
	// A tool window: one of Views' menus (MenuHost), a tooltip.
	tool: boolean;
	bounds: Box;
	// Where its client area starts: its content, inside the frame's edges.
	client: { X: number; Y: number };
	screen: { frame: Box; visible: Box };
};

// The process's visible top-level windows, in DIPs from the main screen's
// top left.
export async function uiaWindows(pid: number): Promise<UiaWindow[]> {
	return JSON.parse(
		await uia("[Console]::Write([PaneUia]::Windows([uint32]$env:PANE_PID))", {
			PANE_PID: String(pid),
		}),
	) as UiaWindow[];
}
