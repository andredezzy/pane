import Foundation

// What the PIN screen does with each key (apps/veil lock-screen/page.tsx):
// verify the current PIN, enter a new one of at least four digits, confirm
// it. The screen draws `dots`, sends the returned action to the app, and
// reports the app's answers back.
public struct PinEntry: Equatable, Sendable {
	public enum Mode: Equatable, Sendable {
		case setup
		case change
		case remove
		// The app is locked; it cannot be dismissed.
		case unlock
	}

	public enum Step: Equatable, Sendable {
		case verify
		case enter
		case confirm
	}

	public enum Action: Equatable, Sendable {
		case none
		// Check `pin` against the stored hash, then call verified(_:remaining:).
		case verify(String)
		// Store `pin` as the new PIN and close.
		case setPin(String)
		// Remove the PIN (verified as `pin`) and close.
		case removePin(String)
		// Unlock the app.
		case unlock
		// The last attempt failed: erase everything.
		case wipe
		// Shake the dots, then call shook().
		case shake
	}

	public static let minimumLength = 4

	public let mode: Mode
	// The stored PIN's digits (none while setting one up).
	public let pinLength: Int
	public private(set) var step: Step
	public private(set) var entered = ""
	public private(set) var firstPin = ""
	public private(set) var error: String?
	// A check is running, or the dots are shaking: keys are ignored.
	public private(set) var busy = false

	public init(mode: Mode, pinLength: Int) {
		self.mode = mode
		self.pinLength = pinLength
		step = mode == .setup ? .enter : .verify
	}

	// The lock with no PIN to check against: it says why, and takes no
	// keys.
	public static func unreadable(_ message: String) -> PinEntry {
		var entry = PinEntry(mode: .unlock, pinLength: minimumLength)
		entry.error = message
		entry.busy = true

		return entry
	}

	public var dismissable: Bool {
		mode != .unlock
	}

	public var title: String? {
		guard mode != .unlock else {
			return nil
		}

		switch step {
		case .verify: return "Enter current PIN"
		case .enter: return "Enter new PIN"
		case .confirm: return "Confirm PIN"
		}
	}

	public var dots: Int {
		switch step {
		case .verify: pinLength
		case .enter: max(entered.count, Self.minimumLength)
		case .confirm: firstPin.count
		}
	}

	// A new PIN is long enough to continue with.
	public var canContinue: Bool {
		step == .enter && entered.count >= Self.minimumLength
	}

	private var submitLength: Int? {
		switch step {
		case .verify: pinLength
		case .enter: nil
		case .confirm: firstPin.count
		}
	}

	public mutating func digit(_ digit: Character) -> Action {
		guard !busy, digit.isASCII, digit.isNumber else {
			return .none
		}

		entered.append(digit)
		error = nil

		guard entered.count == submitLength else {
			return .none
		}

		busy = true

		switch step {
		case .verify:
			return .verify(entered)
		case .confirm:
			if entered == firstPin {
				return .setPin(entered)
			}

			error = "PINs don't match"
			return .shake
		case .enter:
			return .none
		}
	}

	public mutating func backspace() {
		guard !busy else {
			return
		}

		if !entered.isEmpty {
			entered.removeLast()
		}

		error = nil
	}

	// "Continue with n-digit PIN".
	public mutating func continueWithPin() {
		guard canContinue else {
			return
		}

		firstPin = entered
		entered = ""
		step = .confirm
		error = nil
	}

	// The app's answer to .verify.
	public mutating func verified(_ success: Bool, remaining: Int) -> Action {
		guard success else {
			return mode == .unlock && remaining <= 0 ? .wipe : .shake
		}

		switch mode {
		case .unlock:
			return .unlock
		case .change:
			entered = ""
			step = .enter
			busy = false
			error = nil
			return .none
		case .remove:
			return .removePin(entered)
		case .setup:
			return .none
		}
	}

	// The dots stopped shaking: start over.
	public mutating func shook() {
		entered = ""
		busy = false
	}
}
