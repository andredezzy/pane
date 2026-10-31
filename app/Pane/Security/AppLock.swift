import AppKit
import CryptoKit
import Observation
import PaneKit
import os

// The app lock (apps/veil security-store.ts, trpc/routers/security.ts): with
// a PIN set, Pane starts locked behind the PIN screen; the screen also sets,
// changes, and removes the PIN from Settings. The fifth wrong PIN in a row at
// the lock erases everything (the panic wipe) and starts Pane fresh. The PIN
// opens the data key (ADR 0113), sealed in security.json and in its copy
// beside the volume, which DataVault then opens the data with: without a
// PIN, the key opens at launch.
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
	// security.json's copy beside the volume: losing both loses the data.
	@ObservationIgnored private let copy: PersistedFile<SecurityState>
	@ObservationIgnored private let dataDirectory: URL
	@ObservationIgnored private let vault: DataVault
	// The open data key and the secret it is sealed under: setting,
	// changing, or removing the PIN reseals the key with them.
	@ObservationIgnored private var dataKey: SymmetricKey?
	@ObservationIgnored private var secret: Data?

	// The PIN screen's line when no key can be read (Paper 07.6).
	static let keyUnreadable = "Pane can\u{2019}t read the key to your data.\nQuit, then restore security.json."
	static let keychainDenied = "Pane can\u{2019}t read the key to your data.\nOpen Pane again, and allow its keychain item."

	// A security.json that does not decode never opens the lock: its PIN is
	// read from what is left when it can be, its key from the copy beside
	// the volume, and the lock stays shut when neither can. The file is kept
	// aside, as security.json.damaged, before anything writes over it.
	init(dataDirectory: URL, vault: DataVault) {
		let file = PersistedFile<SecurityState>(directory: dataDirectory, name: "security")
		let copy = PersistedFile<SecurityState>(directory: dataDirectory, name: "Data.security")
		var loaded = SecurityState()
		var unreadable = false

		do {
			loaded = try file.load() ?? SecurityState()
		} catch {
			Log.security.error("Could not read security.json: \(String(describing: error), privacy: .private)")

			let damaged = file.url.appendingPathExtension("damaged")

			if !FileManager.default.fileExists(atPath: damaged.path) {
				try? FileManager.default.copyItem(at: file.url, to: damaged)
			}

			if let recovered = (try? Data(contentsOf: file.url)).flatMap(SecurityState.recover) {
				loaded = recovered
			} else {
				unreadable = true
			}
		}

		// The copy stands in for a key security.json lost: the file removed,
		// or no longer decoding.
		if loaded.key == nil, let kept = (try? copy.load()) ?? nil, kept.key != nil {
			loaded = kept
			unreadable = false
		}

		// A volume no key opens: what it holds cannot be read.
		let keyLost = loaded.key == nil && vault.volume.exists

		self.dataDirectory = dataDirectory
		self.file = file
		self.copy = copy
		self.vault = vault
		state = loaded
		isLocked = loaded.pin != nil || unreadable || keyLost

		if unreadable || keyLost {
			present(.unreadable(Self.keyUnreadable))
		} else if let pin = loaded.pin {
			present(PinEntry(mode: .unlock, pinLength: pin.length))
		}
	}

	// With no PIN, the key opens at launch, off the main thread: the
	// Keychain may ask first. With one, the PIN screen opens it.
	func start() {
		guard !isLocked else {
			return
		}

		let state = state
		let attach = vault.attacher()

		Task { [weak self] in
			let opened = await Task.detached(priority: .userInitiated) { Self.open(pin: nil, state: state, attach: attach) }.value
			self?.opened(opened, unlocking: true)
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
			reseal(pin: pin)
		case .removePin:
			reseal(pin: nil)
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

	// What opening the data key came to.
	enum Opened: Sendable {
		// `attaching`, when a saved key opens the data: the volume's attach,
		// started there and then.
		case key(SymmetricKey, secret: Data, made: DataKey.Sealed?, attaching: Task<DataVault.Attached, Error>?)
		case wrongPin
		// The Keychain gave no secret: the user denied it, or it failed.
		case noSecret
		// The key does not open with the secret: another engine key.
		case otherSecret
	}

	// Opens the data key with the PIN, or, before any is sealed, makes it:
	// sealed with the PIN, which matches the bcrypt hash an earlier Pane
	// kept. PBKDF2 and bcrypt take a moment, so it runs off the main thread.
	// `known`: the secret this session read already, so Change and Remove ask
	// the Keychain nothing more. `attach`: the key opens the data, and its
	// volume starts unlocking at once (DataVault.attacher).
	nonisolated static func open(
		pin: String?,
		state: SecurityState,
		secret known: Data? = nil,
		attach: (@Sendable (SymmetricKey) -> Task<DataVault.Attached, Error>)? = nil
	) -> Opened {
		guard let secret = known ?? SafeStorage.dataSecret(makesOne: state.key == nil) else {
			return .noSecret
		}

		let attaching = { (key: SymmetricKey) in attach?(key) }

		if let sealed = state.key {
			do {
				let key = try DataKey.open(sealed, secret: secret, pin: pin)
				return .key(key, secret: secret, made: nil, attaching: attaching(key))
			} catch is DataKey.OtherSecret {
				return .otherSecret
			} catch {
				return .wrongPin
			}
		}

		if let pin {
			guard let hash = state.pin?.hash, PinHash.verify(pin, against: hash) else {
				return .wrongPin
			}
		}

		let key = DataKey.make()

		guard let sealed = try? DataKey.seal(key, secret: secret, pin: pin) else {
			return .noSecret
		}

		// Saved first (opened): a quit before would leave a volume made with
		// no key saved to open it, which Pane takes for a key lost.
		return .key(key, secret: secret, made: sealed, attaching: nil)
	}

	// A wrong PIN counts at once, on disk too, so quitting cannot reset the
	// count. A key that cannot be read at all is not the PIN's fault, and
	// counts nothing.
	private func verify(_ pin: String) {
		let state = state
		let unlocking = entry?.mode == .unlock
		let known = secret
		let attach = unlocking ? vault.attacher() : nil

		Task { [weak self] in
			let opened = await Task.detached(priority: .userInitiated) { Self.open(pin: pin, state: state, secret: known, attach: attach) }.value
			self?.opened(opened, unlocking: unlocking)
		}
	}

	private func opened(_ opened: Opened, unlocking: Bool) {
		switch opened {
		case .noSecret:
			isLocked = true
			present(.unreadable(Self.keychainDenied))
		case .otherSecret:
			isLocked = true
			present(.unreadable(Self.keyUnreadable))
		case .wrongPin where entry == nil:
			// A key sealed with a PIN security.json no longer names.
			isLocked = true
			present(.unreadable(Self.keyUnreadable))
		case .wrongPin:
			state.failedAttempts += 1
			save()
			verified(false)
		case let .key(key, secret, made, attaching):
			state.failedAttempts = 0

			if let made {
				state.key = made
				state.pin?.hash = nil
			}

			save()

			// A key just made is saved now, before the volume it opens is.
			if unlocking {
				dataKey = key
				self.secret = secret
				vault.open(key, attaching: attaching ?? vault.attacher()(key))
			}

			verified(true)
		}
	}

	private func verified(_ matches: Bool) {
		guard var entry else {
			return
		}

		let action = entry.verified(matches, remaining: state.remainingAttempts)
		self.entry = entry
		perform(action)
	}

	// Seals the open key again: with a new PIN (set up, changed), or none
	// (removed). Only the key's seal changes; the data stays as it is.
	private func reseal(pin: String?) {
		guard let dataKey, let secret else {
			Log.security.error("No open data key to seal again")
			return
		}

		Task { [weak self] in
			let sealed = await Task.detached(priority: .userInitiated) { try? DataKey.seal(dataKey, secret: secret, pin: pin) }.value

			guard let self, let sealed else {
				Log.security.error("Could not seal the data key")
				return
			}

			state = SecurityState(pin: pin.map { Pin(length: $0.count) }, failedAttempts: 0, key: sealed)
			save()
			close()
		}
	}

	// security.json, and its copy beside the volume.
	private func save() {
		for file in [file, copy] {
			do {
				try file.save(state)
			} catch {
				Log.security.error("Could not save \(file.url.lastPathComponent, privacy: .public): \(String(describing: error), privacy: .private)")
			}
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

		// The data key goes first: with it gone, nothing left opens the
		// volume or the sealed files, wherever their blocks are (ADR 0113).
		state.key = nil
		dataKey = nil
		save()

		let helpers = Self.childProcesses()

		for pid in helpers {
			kill(pid, SIGKILL)
		}

		if !Self.relaunch(after: [getpid()] + helpers) {
			// No instance to erase: this one does, its engine still running.
			let caches = Self.cacheDirectory(for: dataDirectory)
			let electron = Self.electronData(for: dataDirectory)
			vault.volume.detachLeftovers()
			ProxyPasswordStore(dataDirectory: dataDirectory).delete()
			Self.forgetElectronKey(electron)
			Self.log(
				PanicWipe.stage(dataDirectory: dataDirectory, cacheDirectory: caches, electron: electron)
					+ PanicWipe.eraseStaged(dataDirectory: dataDirectory, cacheDirectory: caches, electron: electron)
			)
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
		let electron = electronData(for: dataDirectory)
		let wiped = CommandLine.arguments.first { $0.hasPrefix(wipeSwitch) }
			.map { $0.dropFirst(wipeSwitch.count).split(separator: ",").compactMap { pid_t($0) } }

		if let wiped {
			let deadline = Date().addingTimeInterval(30)

			while wiped.contains(where: { kill($0, 0) == 0 }), Date() < deadline {
				usleep(50_000)
			}
		}

		if wiped != nil || isWipeDue(dataDirectory) {
			ProxyPasswordStore(dataDirectory: dataDirectory).delete()

			// The wiped instance's volume, which its watchdog went down with:
			// its key is gone, and it moves aside detached.
			DataVolume(dataDirectory: dataDirectory, cacheDirectory: caches).detachLeftovers()

			log(PanicWipe.stage(dataDirectory: dataDirectory, cacheDirectory: caches, electron: electron))
		}

		guard !PanicWipe.staged(dataDirectory: dataDirectory, cacheDirectory: caches, electron: electron).isEmpty else {
			return
		}

		isErasing = true

		let thread = Thread {
			let started = Date()
			let failures = PanicWipe.eraseStaged(dataDirectory: dataDirectory, cacheDirectory: caches, electron: electron)
			// Here, off the launch: the Keychain may take its time.
			forgetElectronKey(electron)
			Log.security.notice("The panic wipe erased its data in \(Date().timeIntervalSince(started), format: .fixed(precision: 1)) s")

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
		let file = PersistedFile<SecurityState>(directory: dataDirectory, name: "security")

		guard let state = (try? file.load()) ?? (try? Data(contentsOf: file.url)).flatMap(SecurityState.recover) else {
			return false
		}

		return state.pin != nil && state.failedAttempts >= SecurityState.maxAttempts
	}

	// Where the engine keeps the profiles' caches, when apart from their data.
	static func cacheDirectory(for dataDirectory: URL) -> URL? {
		let fileManager = FileManager.default

		return PanicWipe.cacheDirectory(
			for: dataDirectory,
			applicationSupport: fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0],
			caches: fileManager.urls(for: .cachesDirectory, in: .userDomainMask)[0]
		)
	}

	// The Electron app's data this Pane migrated from, which the wipe erases
	// too (PanicWipe.ElectronData).
	static func electronData(for dataDirectory: URL) -> PanicWipe.ElectronData? {
		let fileManager = FileManager.default

		return PanicWipe.electronData(
			dataDirectory: dataDirectory,
			applicationSupport: fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0],
			caches: fileManager.urls(for: .cachesDirectory, in: .userDomainMask)[0]
		)
	}

	// The Electron app's cookie key goes with its data, when macOS lets it
	// go without asking: a prompt would ask whoever typed the wrong PINs.
	// Its data erased, the key opens nothing anyway.
	private nonisolated static func forgetElectronKey(_ electron: PanicWipe.ElectronData?) {
		guard
			let directory = electron?.directory,
			let appName = ElectronMigration.appNames.first(where: { directory.path.hasSuffix("/\($0)") })
		else {
			return
		}

		let status = SafeStorage.deleteKey(service: "\(appName) Safe Storage", account: appName)
		Log.security.notice("The panic wipe removed the Electron app's cookie key: \(status)")
	}

	private static func log(_ failures: [PanicWipe.Failure]) {
		for failure in failures {
			Log.security.error("The panic wipe could not erase \(failure.description, privacy: .private)")
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
			Log.security.error("Could not start again after the wipe: \(String(describing: error), privacy: .private)")
			return false
		}
	}

	static let wipeSwitch = "--pane-wipe-after="

	// MARK: Keys

	// The PIN screen takes every key while it shows (digits, Delete, Escape
	// to go back), so nothing reaches a page or the app's shortcuts; only
	// Quit gets through (KeyRouter).
	private func present(_ entry: PinEntry) {
		self.entry = entry
	}

	private func close() {
		entry = nil
	}
}
