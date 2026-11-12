// Prints, as JSON, what a screen reader reaches of an app's windows: each
// element of their accessibility trees, as VoiceOver walks them (AXChildren),
// with its role, subrole, title, description, and value. It reads only; the
// app needs no front, but a locked screen hides every window from it.
// Usage: ax-tree <pid>
import ApplicationServices
import Foundation

guard CommandLine.arguments.count == 2, let pid = Int32(CommandLine.arguments[1]) else {
	FileHandle.standardError.write("usage: ax-tree <pid>\n".data(using: .utf8)!)
	exit(2)
}

func attribute(_ element: AXUIElement, _ name: String) -> CFTypeRef? {
	var value: CFTypeRef?
	return AXUIElementCopyAttributeValue(element, name as CFString, &value) == .success ? value : nil
}

func text(_ element: AXUIElement, _ name: String) -> String {
	guard let value = attribute(element, name) else { return "" }
	if let string = value as? String { return string }
	if let number = value as? NSNumber { return number.stringValue }
	return ""
}

var elements: [[String: Any]] = []

func walk(_ element: AXUIElement, depth: Int) {
	elements.append([
		"depth": depth,
		"role": text(element, kAXRoleAttribute),
		"subrole": text(element, kAXSubroleAttribute),
		"title": text(element, kAXTitleAttribute),
		"description": text(element, kAXDescriptionAttribute),
		"value": text(element, kAXValueAttribute),
	])
	guard depth < 40, let children = attribute(element, kAXChildrenAttribute) as? [AXUIElement] else { return }
	for child in children {
		walk(child, depth: depth + 1)
	}
}

let app = AXUIElementCreateApplication(pid)
for window in attribute(app, kAXWindowsAttribute) as? [AXUIElement] ?? [] {
	walk(window, depth: 0)
}

let data = try JSONSerialization.data(withJSONObject: elements)
FileHandle.standardOutput.write(data)
