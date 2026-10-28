import AppKit
import Observation
import PaneKit

// The app lock (apps/veil security-store.ts, trpc/routers/security.ts): with
// a PIN set, Pane starts locked behind the PIN screen; the screen also sets,
// changes, and removes the PIN from Settings. The fifth wrong PIN in a row at
// the lock erases everything (the panic wipe) and starts Pane fresh.
@MainActor
@Observable
final class AppLock {
	private(set) var state: SecurityState
	// The PIN screen covers the window, and only the PIN opens it.
	private(set) var isLocked: Bool
	// The PIN screen, while it shows.
	private(set) var entry: PinEntry?
	// Bumped to shake the dots.
	private(set) var shakes = 0
	// Called once the right PIN opens the lock, for what waited behind it.
	@ObservationIgnored var didUnlock: (() -> Void)?

	@ObservationIgnored private let file: PersistedFile<SecurityState>
	@ObservationIgnored private let dataDirectory: URL
	@ObservationIgnored private var keyMonitor: Any?

	init(dataDirectory: URL) {
		let file = PersistedFile<SecurityState>(directory: dataDirectory, name: "security")
		var loaded = SecurityState()

		do {
			loaded = try file.load() ?? SecurityState()
		} catch {
			NSLog("Pane: could not read security.json: %@", String(describing: error))
		}

		self.dataDirectory = dataDirectory
		self.file = file
		state = loaded
		isLocked = loaded.pin != nil

		if let pin = loaded.pin {
			present(PinEntry(mode: .unlock, pinLength: pin.length))
		}
	}

	// Settings' Set up, Change, and Remove PIN.
	func show(_ mode: PinEntry.Mode) {
		guard !isLocked, mode != .unlock else {
			return
		}

		present(PinEntry(mode: mode, pinLength: state.pin?.length ?? 0))
	}

	// The back arrow, or Escape; never while locked.
	func dismiss() {
		guard entry?.dismissable == true else {
			return
		}

		close()
	}

	func digit(_ digit: Character) {
		guard var entry else {
			return
		}

		let action = entry.digit(digit)
		self.entry = entry
		perform(action)
	}

	func backspace() {
		entry?.backspace()
	}

	func continueWithPin() {
		entry?.continueWithPin()
	}

	// MARK: Actions

	private func perform(_ action: PinEntry.Action) {
		switch action {
		case .none:
			break
		case let .verify(pin):
			verify(pin)
		case let .setPin(pin):
			setPin(pin)
		case .removePin:
			state = SecurityState()
			save()
			close()
		case .unlock:
			isLocked = false
			close()
			didUnlock?()
		case .wipe:
			wipe()
		case .shake:
			shakes += 1

			// The dots shake for 0.4s; the entry starts over at 0.6s.
			Task { [weak self] in
				try? await Task.sleep(for: .milliseconds(600))
				self?.entry?.shook()
			}
		}
	}

	// bcrypt takes a moment, so it runs off the main thread. A wrong PIN
	// counts at once, on disk too, so quitting cannot reset the count.
	private func verify(_ pin: String) {
		guard let hash = state.pin?.hash else {
			return
		}

		Task { [weak self] in
			let matches = await Task.detached { PinHash.verify(pin, against: hash) }.value

			guard let self, var entry else {
				return
			}

			state.failedAttempts = matches ? 0 : state.failedAttempts + 1
			save()

			let action = entry.verified(matches, remaining: state.remainingAttempts)
			self.entry = entry
			perform(action)
		}
	}

	private func setPin(_ pin: String) {
		Task { [weak self] in
			guard let hash = await Task.detached(operation: { PinHash.make(pin) }).value else {
				NSLog("Pane: could not hash the new PIN")
				return
			}

			guard let self else {
				return
			}

			state = SecurityState(pin: Pin(hash: hash, length: pin.count), failedAttempts: 0)
			save()
			close()
		}
	}

	private func save() {
		do {
			try file.save(state)
		} catch {
			NSLog("Pane: could not save security.json: %@", String(describing: error))
		}
	}

	// MARK: Panic wipe

	// Only after the last attempt at the lock: the count lives on disk, where
	// nothing but a wrong PIN raises it.
	private func wipe() {
		guard state.pin != nil, state.failedAttempts >= SecurityState.maxAttempts else {
			return
		}

		Self.erase(dataDirectory)
		relaunch()
		exit(0)
	}

	// Erases the app's data directory and the engine's caches beside it.
	static func erase(_ dataDirectory: URL) {
		let fileManager = FileManager.default
		let caches = PanicWipe.cacheDirectory(
			for: dataDirectory,
			applicationSupport: fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0],
			caches: fileManager.urls(for: .cachesDirectory, in: .userDomainMask)[0]
		)

		PanicWipe.run(dataDirectory: dataDirectory, cacheDirectory: caches)
	}

	// The wiped app starts again, with this process's arguments and
	// environment; it erases once more after this process exits, in case the
	// engine wrote something on its way out.
	private func relaunch() {
		let process = Process()
		process.executableURL = Bundle.main.executableURL
		process.arguments = CommandLine.arguments.dropFirst().filter { !$0.hasPrefix(Self.wipeSwitch) }
			+ ["\(Self.wipeSwitch)\(getpid())"]

		do {
			try process.run()
		} catch {
			NSLog("Pane: could not start again after the wipe: %@", String(describing: error))
		}
	}

	static let wipeSwitch = "--pane-wipe-after="

	// MARK: Keys

	// The PIN screen takes every key while it shows (digits, Delete, Escape
	// to go back), so nothing reaches a page or the app's shortcuts; only
	// Quit gets through.
	private func present(_ entry: PinEntry) {
		self.entry = entry

		guard keyMonitor == nil else {
			return
		}

		keyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
			// Event monitors run on the main thread.
			nonisolated(unsafe) let event = event
			let consumed = MainActor.assumeIsolated { self?.consumes(event) ?? false }

			return consumed ? nil : event
		}
	}

	private func close() {
		entry = nil

		if let keyMonitor {
			NSEvent.removeMonitor(keyMonitor)
			self.keyMonitor = nil
		}
	}

	// Whether the PIN screen took `event`: all but Quit.
	private func consumes(_ event: NSEvent) -> Bool {
		if event.isQuitShortcut {
			return false
		}

		let flags = event.modifierFlags.intersection(.deviceIndependentFlagsMask)

		switch event.keyCode {
		case 51:
			backspace()
		case 53:
			dismiss()
		case 36, 76:
			continueWithPin()
		default:
			if flags.isDisjoint(with: [.command, .control, .option]), let character = event.characters?.first {
				digit(character)
			}
		}

		return true
	}
}

extension NSEvent {
	// ⌘Q, Caps Lock or not: the key that always reaches Quit.
	var isQuitShortcut: Bool {
		modifierFlags.intersection(.deviceIndependentFlagsMask).subtracting(.capsLock) == .command
			&& charactersIgnoringModifiers?.lowercased() == "q"
	}
}
