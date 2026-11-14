// Chooses an item of an app's menu bar through accessibility, as choosing it
// does: the item found by its titles from the menu's down, then pressed
// (AXPress), with no menu shown, so the app needs no front and nothing shows
// over another app's windows.
// Usage: ax-menu <pid> <menu> <item>...
// Exits 0 once pressed, 1 when a title isn't there, 2 on bad arguments.
import ApplicationServices
import Foundation

let arguments = Array(CommandLine.arguments.dropFirst())
guard arguments.count >= 3, let pid = pid_t(arguments[0]) else {
	FileHandle.standardError.write("usage: ax-menu <pid> <menu> <item>...\n".data(using: .utf8)!)
	exit(2)
}

func children(_ element: AXUIElement) -> [AXUIElement] {
	var value: CFTypeRef?
	guard AXUIElementCopyAttributeValue(element, kAXChildrenAttribute as CFString, &value) == .success
	else { return [] }
	return value as? [AXUIElement] ?? []
}

func title(_ element: AXUIElement) -> String {
	var value: CFTypeRef?
	guard AXUIElementCopyAttributeValue(element, kAXTitleAttribute as CFString, &value) == .success
	else { return "" }
	return value as? String ?? ""
}

let app = AXUIElementCreateApplication(pid)
var menuBar: CFTypeRef?
guard AXUIElementCopyAttributeValue(app, kAXMenuBarAttribute as CFString, &menuBar) == .success,
	let bar = menuBar
else {
	FileHandle.standardError.write("no menu bar for pid \(pid)\n".data(using: .utf8)!)
	exit(1)
}

// A menu bar item's or a menu item's menu is its one child (AXMenu), whose
// children are the items.
var level = children(bar as! AXUIElement)
var found: AXUIElement?
for (index, wanted) in arguments.dropFirst().enumerated() {
	guard let item = level.first(where: { title($0) == wanted }) else {
		FileHandle.standardError.write("no menu item \"\(wanted)\"\n".data(using: .utf8)!)
		exit(1)
	}
	found = item
	if index < arguments.count - 2 {
		level = children(item).flatMap(children)
	}
}

guard let item = found, AXUIElementPerformAction(item, kAXPressAction as CFString) == .success else {
	FileHandle.standardError.write("the menu item would not press\n".data(using: .utf8)!)
	exit(1)
}
