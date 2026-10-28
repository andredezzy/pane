// Prints, as JSON, the frames of a process's accessibility elements whose
// identifier starts with a prefix: { "<identifier>": [x, y, width, height] },
// in points from the top left of the window that holds them. What the window
// lays out, read as it stands, mid-drag included.
//
// Usage: ax-frames <pid> <identifier prefix>
import ApplicationServices
import Foundation

let arguments = CommandLine.arguments

guard arguments.count == 3, let pid = pid_t(arguments[1]) else {
	FileHandle.standardError.write(Data("usage: ax-frames <pid> <identifier prefix>\n".utf8))
	exit(2)
}

let prefix = arguments[2]
let app = AXUIElementCreateApplication(pid)

func attribute(_ element: AXUIElement, _ name: String) -> CFTypeRef? {
	var value: CFTypeRef?
	return AXUIElementCopyAttributeValue(element, name as CFString, &value) == .success ? value : nil
}

func point(_ element: AXUIElement) -> CGPoint? {
	var point = CGPoint.zero
	guard let value = attribute(element, kAXPositionAttribute), AXValueGetValue(value as! AXValue, .cgPoint, &point) else {
		return nil
	}
	return point
}

func size(_ element: AXUIElement) -> CGSize? {
	var size = CGSize.zero
	guard let value = attribute(element, kAXSizeAttribute), AXValueGetValue(value as! AXValue, .cgSize, &size) else {
		return nil
	}
	return size
}

var frames: [String: [Double]] = [:]

func visit(_ element: AXUIElement, origin: CGPoint, depth: Int) {
	if let identifier = attribute(element, kAXIdentifierAttribute) as? String, identifier.hasPrefix(prefix),
	   let at = point(element), let extent = size(element) {
		frames[identifier] = [at.x - origin.x, at.y - origin.y, extent.width, extent.height]
	}

	guard depth < 40, let children = attribute(element, kAXChildrenAttribute) as? [AXUIElement] else {
		return
	}

	for child in children {
		visit(child, origin: origin, depth: depth + 1)
	}
}

for window in attribute(app, kAXWindowsAttribute) as? [AXUIElement] ?? [] {
	if let origin = point(window) {
		visit(window, origin: origin, depth: 0)
	}
}

let json = try JSONSerialization.data(withJSONObject: frames, options: [.sortedKeys])
print(String(decoding: json, as: UTF8.self))
