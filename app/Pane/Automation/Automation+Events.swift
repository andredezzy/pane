import AppKit
import PaneKit

// Real input, as the trackpad and the keyboard make it: one builder for
// mouse events and one for key events, each in the main window.
extension Automation {
	static let keyCodes: [String: UInt16] = [
		"a": 0, "f": 3, "i": 34, "k": 40, "p": 35, "q": 12, "r": 15, "t": 17, "u": 32, "w": 13, "l": 37, "y": 16,
		"1": 18, "2": 19, "3": 20, "0": 29, "=": 24, "-": 27,
		"[": 33, "]": 30, ",": 43, "\t": KeyCode.tab, "\r": KeyCode.returnKey, "\u{1b}": KeyCode.escape,
	]

	private var window: NSWindow {
		controller.browserWindow.window
	}

	// A point of the window, from its top left as scenarios give it.
	func windowPoint(_ point: NSPoint) -> NSPoint {
		NSPoint(x: point.x, y: window.frame.height - point.y)
	}

	// MARK: Builders

	func mouseEvent(_ type: NSEvent.EventType, at point: NSPoint, clickCount: Int = 1) -> NSEvent? {
		NSEvent.mouseEvent(
			with: type,
			location: point,
			modifierFlags: [],
			timestamp: ProcessInfo.processInfo.systemUptime,
			windowNumber: window.windowNumber,
			context: nil,
			eventNumber: 0,
			clickCount: clickCount,
			pressure: type == .leftMouseUp ? 0 : 1
		)
	}

	func keyEvent(_ type: NSEvent.EventType, keyCode: UInt16, characters: String, modifiers: NSEvent.ModifierFlags = []) -> NSEvent? {
		NSEvent.keyEvent(
			with: type,
			location: .zero,
			modifierFlags: modifiers,
			timestamp: ProcessInfo.processInfo.systemUptime,
			windowNumber: window.windowNumber,
			context: nil,
			characters: characters,
			charactersIgnoringModifiers: characters,
			isARepeat: false,
			keyCode: keyCode
		)
	}

	// MARK: The mouse

	// Down then up, `clicks` times, in the event queue as the trackpad's.
	func postClick(at point: NSPoint, clicks: Int) {
		for count in 1 ... max(clicks, 1) {
			for type in [NSEvent.EventType.leftMouseDown, .leftMouseUp] {
				if let event = mouseEvent(type, at: point, clickCount: count) {
					NSApp.postEvent(event, atStart: false)
				}
			}
		}
	}

	// One phase of a drag. A move carries its distance from the last, as
	// the trackpad's do: gesture recognizers measure a drag by it.
	func postPointer(_ type: NSEvent.EventType, at point: NSPoint) {
		guard let event = mouseEvent(type, at: point), let moved = event.cgEvent?.copy() else {
			return
		}

		let last = type == .leftMouseDown ? point : (lastPointer ?? point)
		moved.setDoubleValueField(.mouseEventDeltaX, value: point.x - last.x)
		moved.setDoubleValueField(.mouseEventDeltaY, value: last.y - point.y)
		lastPointer = point
		NSApp.postEvent(NSEvent(cgEvent: moved) ?? event, atStart: false)
	}

	// A drag dispatched through the window, from and to points of it (top
	// left origin); each step's time to the window's layout, in ms.
	func drag(from: NSPoint, to: NSPoint, steps: Int) -> [Double] {
		var times: [Double] = []

		if let down = mouseEvent(.leftMouseDown, at: windowPoint(from)) {
			window.sendEvent(down)
		}

		for step in 1 ... steps {
			let fraction = Double(step) / Double(steps)
			let at = windowPoint(NSPoint(x: from.x + (to.x - from.x) * fraction, y: from.y + (to.y - from.y) * fraction))
			let started = CACurrentMediaTime()

			if let move = mouseEvent(.leftMouseDragged, at: at) {
				window.sendEvent(move)
			}

			controller.hostingView?.layoutSubtreeIfNeeded()
			times.append((CACurrentMediaTime() - started) * 1000)
		}

		if let up = mouseEvent(.leftMouseUp, at: windowPoint(to)) {
			window.sendEvent(up)
		}

		return times
	}

	// A trackpad scroll by `dy` points at a point of the window.
	func scrollEvent(at point: NSPoint, dy: Double) throws -> NSEvent {
		let onScreen = window.convertPoint(toScreen: point)
		let top = NSScreen.screens.first?.frame.maxY ?? 0

		guard
			let scroll = CGEvent(scrollWheelEvent2Source: nil, units: .pixel, wheelCount: 1, wheel1: Int32(dy), wheel2: 0, wheel3: 0)
		else {
			throw AutomationError("could not make the scroll")
		}

		scroll.location = CGPoint(x: onScreen.x, y: top - onScreen.y)

		guard let event = NSEvent(cgEvent: scroll) else {
			throw AutomationError("could not make the scroll")
		}

		return event
	}

	// MARK: The keyboard

	// Down then up, after the modifiers change, in the event queue: event
	// monitors, then the menu's key equivalents, then the window.
	func postKey(_ key: String, modifiers: [String]) throws {
		guard let keyCode = Self.keyCodes[key] else {
			throw AutomationError("no key code for \(key)")
		}

		let flags = Self.modifierFlags(modifiers)
		// As typed: Shift makes a letter uppercase.
		let characters = flags.contains(.shift) ? key.uppercased() : key

		if !flags.isEmpty {
			post(flagsChanged: flags)
		}

		for type in [NSEvent.EventType.keyDown, .keyUp] {
			if let event = keyEvent(type, keyCode: keyCode, characters: characters, modifiers: flags) {
				NSApp.postEvent(event, atStart: false)
			}
		}
	}

	func post(flagsChanged flags: NSEvent.ModifierFlags) {
		let event = NSEvent.keyEvent(
			with: .flagsChanged,
			location: .zero,
			modifierFlags: flags,
			timestamp: ProcessInfo.processInfo.systemUptime,
			windowNumber: window.windowNumber,
			context: nil,
			characters: "",
			charactersIgnoringModifiers: "",
			isARepeat: false,
			keyCode: flags.contains(.control) ? KeyCode.leftControl : 0
		)

		if let event {
			NSApp.postEvent(event, atStart: false)
		}
	}

	static func modifierFlags(_ names: [String]) -> NSEvent.ModifierFlags {
		names.reduce(into: NSEvent.ModifierFlags()) { flags, name in
			switch name {
			case "command": flags.insert(.command)
			case "shift": flags.insert(.shift)
			case "control": flags.insert(.control)
			case "option": flags.insert(.option)
			case "capsLock": flags.insert(.capsLock)
			default: break
			}
		}
	}
}
