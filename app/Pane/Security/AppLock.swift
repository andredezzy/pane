import AppKit
import CryptoKit
import Observation
import PaneKit
import os

// The app lock (apps/veil security-store.ts, trpc/routers/security.ts): with
// a PIN set, Pane starts locked behind the PIN screen; the screen also sets,
// changes, and removes the PIN from Settings. The fifth wrong PIN in a row at
// the lock erases everything (the panic wipe) and starts Pane fresh; the fifth
// in Settings only locks Pane (ADR 0042). The PIN
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
	// Called at the fifth wrong PIN in Settings, for Pane to lock.
	@ObservationIgnored var didExhaustSettingsAttempts: (() -> Void)?

	@ObservationIgnored private let file: PersistedFile<SecurityState>
	// security.json's copy beside the volume: losing both loses the data.
	@ObservationIgnored private let copy: PersistedFile<SecurityState>
	@ObservationIgnored private let dataDirectory: URL
	@ObservationIgnored private let vault: DataVault
	// The secret the open data key is sealed under: setting, changing, or
	// removing the PIN reseals the key with it.
	@ObservationIgnored private var secret: Data?
	// Wrong PINs in Settings in a row, in memory only: the count on disk is the
	// lock's alone, where its fifth erases everything.
	@ObservationIgnored private var settingsAttempts = 0

	// The PIN screen's line when no key can be read (Paper 07.6).
	static let keyUnreadable = "Pane can\u{2019}t read the key to your data.\nQuit, then restore security.json."
	static let keychainDenied = "Pane can\u{2019}t read the key to your data.\nOpen Pane again, and allow its keychain item."
	// And when the volume would not open (07.8).
	static let dataWontOpen = "Pane can\u{2019}t open your encrypted data.\nQuit, then open Pane again."

	// A security.json that does not decode never opens the lock: its key is read
	// from the copy beside the volume, and the lock stays shut without one. The
	// file is kept aside, as security.json.damaged, before anything writes over it.
	init(dataDirectory: URL, vault: DataVault) {
		let file = PersistedFile<SecurityState>(directory: dataDirectory, name: "security")
		let copy = PersistedFile<SecurityState>(directory: dataDirectory, name: "Data.security")
		var loaded = SecurityState()
		var unreadable = false

		do {
			loaded = try file.load() ?? SecurityState()
		} catch {
			Log.security.error("Could not read security.json: \(String(describing: error), privacy: .private)")
			keepDamaged(file.url)
			unreadable = true
		}

		// The copy stands in for a key security.json lost: the file removed,
		// or no longer decoding.
		if loaded.key == nil, let kept = (try? copy.load()) ?? nil, kept.key != nil {
			loaded = kept
			unreadable = false
		}

		// The lock's fifth attempt takes the key in the same save, so the last
		// attempt with a key still there came from Settings in 0.18.0, where wrong
		// PINs counted: it leaves the lock its attempts, or one typo would erase.
		if loaded.failedAttempts >= SecurityState.maxAttempts, loaded.key != nil {
			loaded.failedAttempts = 0
		}

		// A volume no key opens: what it holds cannot be read. Nor is a PIN with no
		// key checked, the bcrypt hash of a Pane before 0.14 (ADR 0122): it counts no
		// attempts, since the fifth erases, and makes no key.
		let keyLost = loaded.key == nil && (vault.volume.exists || loaded.pin != nil)

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

	// Pane locks while open (ADR 0116, Paper 07.9): the PIN screen shows at
	// once, as at launch, and the data key goes from memory; the caller
	// closes every profile's data behind it. Only a PIN locks.
	func lockNow() {
		guard let pin = state.pin, !isLocked else {
			return
		}

		isLocked = true
		present(PinEntry(mode: .unlock, pinLength: pin.length))
	}

	// The volume would not open once the key had (DataVault): nothing of the
	// profiles can run, so the lock shuts, says so, and takes no keys. With
	// a PIN, the panel that slid away comes back.
	func volumeWontOpen() {
		isLocked = true
		present(.unreadable(Self.dataWontOpen))
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

	// Opens the data key with the PIN, or, with no PIN and none sealed yet, makes
	// it. PBKDF2 takes a moment, so it runs off the main thread.
	// `known`: the secret this session read already, so Change and Remove ask
	// the Keychain nothing more. `attach`: the key opens the data, and its
	// volume starts unlocking at once (DataVault.attacher).
	nonisolated static func open(
		pin: String?,
		state: SecurityState,
		secret known: Data? = nil,
		attach: (@Sendable (SymmetricKey) -> Task<DataVault.Attached, Error>)? = nil
	) -> Opened {
		// A PIN with no key sealed stops at init: never a key made under it, and
		// never an attempt counted for it.
		guard pin == nil || state.key != nil else {
			return .otherSecret
		}

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

		let key = DataKey.make()

		guard let sealed = try? DataKey.seal(key, secret: secret, pin: nil) else {
			return .noSecret
		}

		// Saved first (opened): a quit before would leave a volume made with
		// no key saved to open it, which Pane takes for a key lost.
		return .key(key, secret: secret, made: sealed, attaching: nil)
	}

	// A wrong PIN at the lock counts at once, on disk too, so quitting cannot
	// reset the count. A key that cannot be read at all is not the PIN's fault,
	// and counts nothing.
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
		case .wrongPin where unlocking:
			state.failedAttempts += 1

			// The last attempt and the key go in one save: with the key gone,
			// nothing left opens the volume or the sealed files, wherever their
			// blocks are (ADR 0113), and a wipe is due (isWipeDue).
			if state.failedAttempts >= SecurityState.maxAttempts {
				state.key = nil
			}

			save()
			verified(false)
		case .wrongPin:
			settingsAttempts += 1
			verified(false)

			if settingsAttempts >= SecurityState.maxAttempts {
				settingsAttempts = 0
				didExhaustSettingsAttempts?()
			}
		case let .key(key, secret, made, attaching):
			state.failedAttempts = 0
			settingsAttempts = 0

			if let made {
				state.key = made
			}

			save()

			// A key just made is saved now, before the volume it opens is.
			if unlocking {
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
		guard let dataKey = vault.dataKey, let secret else {
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

	// security.json, and its copy beside the volume. One that already says this
	// is left alone: a launch that goes no further changes nothing of the data.
	private func save() {
		for file in [file, copy] where (try? file.load()) != state {
			do {
				try file.save(state)
			} catch {
				Log.security.error("Could not save \(file.url.lastPathComponent, privacy: .public): \(String(describing: error), privacy: .private)")
			}
		}
	}

	// MARK: Panic wipe

	// Only after the last attempt at the lock: the count lives on disk, where
	// nothing but a wrong PIN there raises it. This instance only ends. Its helpers
	// go first, so none (the network service, say) writes after, and the
	// instance it starts erases once they are all gone (PanicWipeRun.atLaunch).
	private func wipe() {
		guard state.pin != nil, state.failedAttempts >= SecurityState.maxAttempts else {
			return
		}

		let helpers = PanicWipeRun.childProcesses()

		for pid in helpers {
			kill(pid, SIGKILL)
		}

		if !PanicWipeRun.relaunch(after: [getpid()] + helpers) {
			// No instance to erase: this one does, its engine still running.
			let caches = Self.cacheDirectory(for: dataDirectory)
			vault.volume.detachLeftovers()
			PanicWipeRun.log(
				PanicWipe.stage(dataDirectory: dataDirectory, cacheDirectory: caches)
					+ PanicWipe.eraseStaged(dataDirectory: dataDirectory, cacheDirectory: caches)
			)
		}

		// Not exit(): nothing of the engine's shutdown runs, which would write.
		_exit(0)
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
