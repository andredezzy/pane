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
	// nothing but a wrong PIN raises it. This instance only ends. Its helpers
	// go first, so none (the network service, say) writes after, and the
	// instance it starts erases once they are all gone (wipeAtLaunch).
	private func wipe() {
		guard state.pin != nil, state.failedAttempts >= SecurityState.maxAttempts else {
			return
		}

		let helpers = Self.childProcesses()

		for pid in helpers {
			kill(pid, SIGKILL)
		}

		if !Self.relaunch(after: [getpid()] + helpers) {
			// No instance to erase: this one does, its engine still running.
			let caches = Self.cacheDirectory(for: dataDirectory)
			Self.log(PanicWipe.stage(dataDirectory: dataDirectory, cacheDirectory: caches) + PanicWipe.eraseStaged(dataDirectory: dataDirectory, cacheDirectory: caches))
		}

		// Not exit(): nothing of the engine's shutdown runs, which would write.
		_exit(0)
	}

	// While the erase runs on its thread, and what it could not erase: for
	// the security scenario.
	private(set) static var isErasing = false
	private(set) static var eraseFailures: [String] = []

	// At launch, before anything reads the data directory. The wiped
	// instance's switch names it and its helpers, which this waits out. A
	// wipe is due too when security.json still holds the last attempt: the
	// wiped instance died before starting this one. The data moves out of the
	// way at once, so Pane starts fresh, and is erased on a thread; data a
	// wipe cut short left (a crash, a quit) is erased the same way.
	static func wipeAtLaunch(_ dataDirectory: URL) {
		let caches = cacheDirectory(for: dataDirectory)
		let wiped = CommandLine.arguments.first { $0.hasPrefix(wipeSwitch) }
			.map { $0.dropFirst(wipeSwitch.count).split(separator: ",").compactMap { pid_t($0) } }

		if let wiped {
			let deadline = Date().addingTimeInterval(30)

			while wiped.contains(where: { kill($0, 0) == 0 }), Date() < deadline {
				usleep(50_000)
			}
		}

		if wiped != nil || isWipeDue(dataDirectory) {
			log(PanicWipe.stage(dataDirectory: dataDirectory, cacheDirectory: caches))
		}

		guard !PanicWipe.staged(dataDirectory: dataDirectory, cacheDirectory: caches).isEmpty else {
			return
		}

		isErasing = true

		let thread = Thread {
			let started = Date()
			let failures = PanicWipe.eraseStaged(dataDirectory: dataDirectory, cacheDirectory: caches)
			NSLog("Pane: the panic wipe erased its data in %.1f s", Date().timeIntervalSince(started))

			Task { @MainActor in
				log(failures)
				eraseFailures = failures.map(\.description)
				isErasing = false
			}
		}
		thread.qualityOfService = .utility
		thread.start()
	}

	private static func isWipeDue(_ dataDirectory: URL) -> Bool {
		guard let state = try? PersistedFile<SecurityState>(directory: dataDirectory, name: "security").load() else {
			return false
		}

		return state.pin != nil && state.failedAttempts >= SecurityState.maxAttempts
	}

	private static func cacheDirectory(for dataDirectory: URL) -> URL? {
		let fileManager = FileManager.default

		return PanicWipe.cacheDirectory(
			for: dataDirectory,
			applicationSupport: fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0],
			caches: fileManager.urls(for: .cachesDirectory, in: .userDomainMask)[0]
		)
	}

	private static func log(_ failures: [PanicWipe.Failure]) {
		for failure in failures {
			NSLog("Pane: the panic wipe could not erase %@", failure.description)
		}
	}

	// The engine's helpers: renderers, the GPU and network services, and
	// Crashpad's handler.
	private static func childProcesses() -> [pid_t] {
		let size = proc_listpids(UInt32(PROC_PPID_ONLY), UInt32(getpid()), nil, 0)

		guard size > 0 else {
			return []
		}

		var pids = [pid_t](repeating: 0, count: Int(size) / MemoryLayout<pid_t>.size + 16)
		let filled = proc_listpids(UInt32(PROC_PPID_ONLY), UInt32(getpid()), &pids, Int32(pids.count * MemoryLayout<pid_t>.size))

		return pids.prefix(max(0, Int(filled)) / MemoryLayout<pid_t>.size).filter { $0 > 0 }
	}

	// The wiped app starts again, with this process's arguments and
	// environment, and waits for `processes` to exit before it erases.
	private static func relaunch(after processes: [pid_t]) -> Bool {
		let process = Process()
		process.executableURL = Bundle.main.executableURL
		process.arguments = CommandLine.arguments.dropFirst().filter { !$0.hasPrefix(wipeSwitch) }
			+ ["\(wipeSwitch)\(processes.map(String.init).joined(separator: ","))"]

		do {
			try process.run()
			return true
		} catch {
			NSLog("Pane: could not start again after the wipe: %@", String(describing: error))
			return false
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
