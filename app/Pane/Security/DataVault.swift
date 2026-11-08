import CryptoKit
import Foundation
import Observation
import PaneKit
import os

// What the data key opens (ADR 0113): Pane's own files, sealed in place, and
// the encrypted volume every profile's engine data lives in. AppLock opens
// the key, with the PIN when there is one, and hands it here. The volume
// attaches on a thread while the window already shows the profiles; a
// profile's context opens only once it is attached and the profile's links
// are in place (ProfileContexts.with). The first open after an earlier Pane moves
// its plain profile folders in, with the lock's panel showing the move (Paper 07.5).
@MainActor
@Observable
final class DataVault {
	enum Phase: Equatable {
		// Nothing of the profiles is read yet.
		case closed
		// An earlier Pane's plain folders moving in: the share copied.
		case moving(Double)
		case open

		// The share copied, while moving.
		var share: Double? {
			if case let .moving(share) = self {
				share
			} else {
				nil
			}
		}
	}

	private(set) var phase: Phase = .closed
	@ObservationIgnored let volume: DataVolume
	// The data key while unlocked: for Pane's own files, for each profile's engine
	// key (ADR 0121), and to seal again with a new PIN.
	@ObservationIgnored private(set) var dataKey: SymmetricKey?
	// Why the volume would not attach, for the log and the security scenario.
	@ObservationIgnored private(set) var failure: String?
	// The attached image's device, which detaches it whole.
	@ObservationIgnored private var device: String?
	// The image's attach at launch, its volume locked (attachImage).
	@ObservationIgnored private var image: Task<DataVolume.Devices?, Never>?
	// The data's lock, held from launch, or taken once the Pane that held it
	// let go: the image is this one's alone while it is (DataLock).
	@ObservationIgnored private var dataLock: DataLock?
	// Told what opens and what will not.
	@ObservationIgnored weak var controller: BrowserController?
	// The attach the key started, for the quit to wait for (close).
	@ObservationIgnored private var attaching: Task<Attached, Error>?
	// A lock's detach, which the next attach waits for (lock): the device it
	// left attached, when a profile would not close.
	@ObservationIgnored private var closing: Task<String?, Never>?
	// What the open does once attached (finish), for a lock to wait for.
	@ObservationIgnored private var finishing: Task<Void, Never>?
	// Locks so far: an attach or move a lock overtook reports to no one.
	@ObservationIgnored private var locks = 0
	// When the attach started and ended, in milliseconds since 1970.
	@ObservationIgnored private(set) var attachedAt: (started: Double, ended: Double)?
	@ObservationIgnored private var attachWaiters: [(Bool) -> Void] = []
	@ObservationIgnored private let dataDirectory: URL

	// The data's lock and the image's attach from main.swift, whose device the
	// quit detaches: neither, when another Pane held the data at launch.
	init(dataDirectory: URL, dataLock: DataLock?, image: Task<DataVolume.Devices?, Never>?) {
		self.dataDirectory = dataDirectory
		self.dataLock = dataLock
		self.image = image
		volume = DataVolume(dataDirectory: dataDirectory, cacheDirectory: AppLock.cacheDirectory(for: dataDirectory))
	}

	// An earlier Pane's profiles wait to move in with no PIN to ask first:
	// the panel shows the move from the first frame, as the PIN screen shows
	// from it (ADR 0042).
	func expectMove() {
		phase = .moving(0)
	}

	// The key opened, and `attaching`, started where it opened, attaches the
	// volume. Profiles an earlier Pane left in the clear move in before any
	// opens: on the first open, or one after a quit cut that move short.
	func open(_ key: SymmetricKey, attaching: Task<Attached, Error>) {
		dataKey = key
		self.attaching = attaching
		controller?.dataKeyDidOpen()

		let ids = profileIDs
		let moving = ids.contains(where: volume.hasPlainFolder)

		if moving {
			phase = .moving(0)
		} else {
			becomeOpen()
		}

		finish(attaching, moving: moving ? ids : nil)
	}

	// Pane's own files' key, once the data key opened.
	var filesKey: SymmetricKey? {
		dataKey.map(DataKey.filesKey)
	}

	var isAttached: Bool {
		device != nil
	}

	// The device the attach returned, and when it started and ended, in
	// milliseconds since 1970: for the security scenario and benchmark-unlock.
	// A task's value, handed from the task that waits for it (settled).
	private final class Settled<Value>: @unchecked Sendable {
		var value: Value?
	}

	// Another Pane held the data past the wait for its lock.
	private struct Held: Error, CustomStringConvertible {
		var description: String {
			"another Pane holds this data"
		}
	}

	struct Attached: Sendable {
		let device: String
		let started: Double
		let ended: Double
		// Taken for the attach, by a Pane started while another held it.
		let lock: DataLock?
	}

	// The image attaches as Pane starts, its volume locked, while the engine
	// starts and the PIN is typed, with its watchdog: what is left once the
	// key opens is the unlock and the mount. main.swift starts it before the
	// engine's singleton, holding the data's lock, so a Pane started while
	// another runs never starts it: that one's image stays its own. One that
	// fails here is attached again, leftovers cleared, once the key opens.
	nonisolated static func attachImage(_ volume: DataVolume) -> Task<DataVolume.Devices?, Never> {
		Task.detached(priority: .userInitiated) {
			guard volume.exists, let devices = try? volume.attachImageOnce() else {
				return nil
			}

			watch(volume)
			return devices
		}
	}

	// What attaches the volume once the key opens, off the main thread, from
	// wherever it opened: the main thread, busy at launch and as the lock
	// slides away, never holds it back.
	func attacher() -> @Sendable (SymmetricKey) -> Task<Attached, Error> {
		let volume = volume
		let image = image
		let locking = dataLock == nil ? dataDirectory : nil
		let closing = closing

		return { key in
			Task.detached(priority: .userInitiated) {
				_ = await closing?.value
				return try await Self.attach(volume, key: key, image: image, locking: locking)
			}
		}
	}

	// Unlocks and mounts the volume the image attached at launch, or attaches
	// it now, or makes it the first time. A Pane started while another held
	// the data takes its lock first, once that one has let go: it quit, and
	// detached the image before, or the engine's singleton ended it.
	private nonisolated static func attach(
		_ volume: DataVolume,
		key: SymmetricKey,
		image: Task<DataVolume.Devices?, Never>?,
		locking dataDirectory: URL?
	) async throws -> Attached {
		let started = Date().timeIntervalSince1970 * 1000
		let passphrase = DataKey.volumePassphrase(key)
		let former = DataKey.formerVolumePassphrase(key)
		var lock: DataLock?

		if let dataDirectory {
			lock = DataLock.wait(dataDirectory: dataDirectory, timeout: 30)

			guard lock != nil else {
				throw Held()
			}
		}

		let devices: DataVolume.Devices

		if let attached = await image?.value {
			devices = attached
		} else {
			devices = try volume.attachOrMake(passphrase: passphrase)
			watch(volume)
		}

		try volume.open(devices, passphrase: passphrase, former: former)

		let ended = Date().timeIntervalSince1970 * 1000
		Log.security.notice("The data volume opened in \((ended - started) / 1000, format: .fixed(precision: 2)) s")

		return Attached(device: devices.image, started: started, ended: ended, lock: lock)
	}

	// One watchdog for this Pane's life, once started: it finds the image by its
	// path.
	private nonisolated static let watchdog = OSAllocatedUnfairLock<pid_t?>(initialState: nil)

	private nonisolated static func watch(_ volume: DataVolume) {
		watchdog.withLock { running in
			guard running == nil else {
				return
			}

			guard let helper = Bundle.main.url(forAuxiliaryExecutable: "pane-volume-watch") else {
				Log.security.error("No pane-volume-watch in the bundle: a crash leaves the volume attached")
				return
			}

			do {
				running = try volume.watch(helper: helper)
			} catch {
				Log.security.error("Could not start the volume's watchdog: \(String(describing: error), privacy: .private)")
			}
		}
	}

	// The image is off, so nothing is left for the watchdog, which would hold the
	// data's lock through its hdiutil: a Pane started in those 250 ms finds the
	// lock taken, and its image attaches only once the key opens (ADR 0113).
	private nonisolated static func endWatchdog() {
		watchdog.withLock { running in
			if let running {
				kill(running, SIGTERM)
			}
		}
	}

	// The key a profile's engine files are under, while unlocked.
	func engineKey(profile id: String) -> Data? {
		dataKey.map { DataKey.engineKey($0, profile: id).withUnsafeBytes { Data($0) } }
	}

	// Runs `body` once the volume is attached, with false when it would not
	// attach.
	func whenAttached(_ body: @escaping (Bool) -> Void) {
		if isAttached {
			body(true)
		} else if failure != nil {
			body(false)
		} else {
			attachWaiters.append(body)
		}
	}

	// The profile's folders in the volume and its links, off the main thread:
	// a plain folder an earlier Pane left still moves in.
	func prepare(profile id: String, savesHistory: Bool, _ done: @escaping (Bool) -> Void) {
		let volume = volume

		Task {
			let prepared = await Task.detached(priority: .userInitiated) { () -> Bool in
				do {
					try volume.prepare(profile: id, savesHistory: savesHistory)
					return true
				} catch {
					Log.security.error("Could not prepare a profile's folders: \(String(describing: error), privacy: .private)")
					return false
				}
			}.value

			guard prepared else {
				done(false)
				return
			}

			ensureFilesEncrypted(profile: id, waiting: true, done)
		}
	}

	// MARK: Encrypting a profile's files (ADR 0121)

	// The engine's way to encrypt or restore a profile's folder.
	struct FileMigrator {
		var migrate: (_ id: String, _ key: Data, _ folder: URL, _ progress: @escaping @Sendable (Double) -> Void, _ done: @escaping (Bool) -> Void) -> Void
		var restore: (_ id: String, _ key: Data, _ folder: URL, _ done: @escaping (Bool) -> Void) -> Void
		var repair: (_ id: String, _ key: Data, _ folder: URL, _ done: @escaping (Bool) -> Void) -> Void
	}

	@ObservationIgnored var fileMigrator: FileMigrator?
	// The profile each migration under way is for, and who waits for it.
	@ObservationIgnored private var migrations: [String: [(Bool) -> Void]] = [:]
	// The profile whose migration the panel shows: one the window waits for, past a
	// second (Paper 07.5).
	@ObservationIgnored private var showing: String?
	@ObservationIgnored private var migrationShares: [String: Double] = [:]
	@ObservationIgnored private var migrationsIdle: [CheckedContinuation<Void, Never>] = []

	// Runs `done` once the profile's files are encrypted. One a window waits for
	// shows the panel past a second; the others run behind it, one at a time. A
	// migration and a restore never share a folder: each waits for the other, and
	// a lock's detach for both (migrationsFinished).
	private func ensureFilesEncrypted(profile id: String, waiting: Bool, _ done: @escaping (Bool) -> Void) {
		if migrations[id] != nil {
			if waiting {
				showMigrationPastASecond(profile: id)
			}

			migrations[id]?.append { [weak self] _ in
				self?.ensureFilesEncrypted(profile: id, waiting: waiting, done)
			}
			return
		}

		let step = volume.safetyCopyWillOpen(profile: id)

		if step == .restore, let key = engineKey(profile: id), let fileMigrator {
			migrations[id] = []

			fileMigrator.restore(id, key, volume.folder(profile: id)) { [weak self] restored in
				guard let self else {
					return
				}

				let waiters = migrations.removeValue(forKey: id) ?? []

				if restored {
					volume.safetyCopyDidRestore(profile: id)
					ensureFilesEncrypted(profile: id, waiting: waiting, done)
				} else {
					Log.security.error("Could not restore a profile's safety copy")
					done(false)
				}

				endMigration(waiters, restored)
			}
			return
		}

		migrate(profile: id, waiting: waiting, done)
	}

	// What waited for a migration or a restore that ended goes on, and a lock's
	// detach, once none is left.
	private func endMigration(_ waiters: [(Bool) -> Void], _ result: Bool) {
		waiters.forEach { $0(result) }

		if migrations.isEmpty {
			migrationsIdle.forEach { $0.resume() }
			migrationsIdle = []
		}
	}

	// One a window waits for shows, however it started.
	private func showMigrationPastASecond(profile id: String) {
		let locks = locks

		DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [weak self] in
			guard let self, locks == self.locks, let share = self.migrationShares[id], self.phase == .open else {
				return
			}

			self.showing = id
			self.phase = .moving(share)
		}
	}

	private func migrate(profile id: String, waiting: Bool, _ done: @escaping (Bool) -> Void) {
		// A profile made without a tab has no folder: nothing to encrypt.
		guard volume.hasFolder(profile: id) else {
			done(true)
			return
		}

		guard !volume.filesAreEncrypted(profile: id) else {
			repair(profile: id, done)
			return
		}

		if waiting {
			showMigrationPastASecond(profile: id)
		}

		if migrations[id] != nil {
			migrations[id]?.append(done)
			return
		}

		guard let key = engineKey(profile: id), let fileMigrator else {
			done(false)
			return
		}

		migrations[id] = [done]
		migrationShares[id] = 0
		let locks = locks

		fileMigrator.migrate(id, key, volume.folder(profile: id), { [weak self] value in
			Task { @MainActor in
				guard let self else {
					return
				}

				self.migrationShares[id] = value

				if self.showing == id {
					self.moved(value, locks: locks)
				}
			}
		}, { [weak self] migrated in
			guard let self else {
				return
			}

			if migrated {
				volume.markFilesEncrypted(profile: id)
			} else {
				Log.security.error("Could not encrypt a profile's files")
			}

			let waiters = migrations.removeValue(forKey: id) ?? []
			migrationShares[id] = nil

			if showing == id {
				showing = nil

				if locks == self.locks {
					becomeOpen()
				}
			}

			endMigration(waiters, migrated)
		})
	}

	// What a migration before 0.18.2 left wrong in a profile, put right once before
	// it next opens. One that fails opens as it was, and tries again next time.
	private func repair(profile id: String, _ done: @escaping (Bool) -> Void) {
		guard !volume.filesAreRepaired(profile: id) else {
			done(true)
			return
		}

		if migrations[id] != nil {
			migrations[id]?.append(done)
			return
		}

		guard let key = engineKey(profile: id), let fileMigrator else {
			done(false)
			return
		}

		migrations[id] = []

		fileMigrator.repair(id, key, volume.folder(profile: id)) { [weak self] repaired in
			guard let self else {
				return
			}

			if repaired {
				volume.markFilesRepaired(profile: id)
			} else {
				Log.security.error("Could not repair a profile's files")
			}

			let waiters = migrations.removeValue(forKey: id) ?? []
			done(true)
			endMigration(waiters, true)
		}
	}

	// Every profile not yet encrypted, one at a time, behind what the window shows.
	func encryptRemainingProfiles(_ ids: [String]) {
		guard let id = ids.first else {
			return
		}

		let locks = locks

		migrate(profile: id, waiting: false) { [weak self] _ in
			guard let self, locks == self.locks else {
				return
			}

			encryptRemainingProfiles(Array(ids.dropFirst()))
		}
	}

	// A lock's detach waits for a migration under way: it has the volume's files open.
	private func migrationsFinished() async {
		guard !migrations.isEmpty else {
			return
		}

		await withCheckedContinuation { migrationsIdle.append($0) }
	}

	// The profile came up: past 5 s, its safety copy counts this open. A lock
	// inside them closed the open, so its timer counts nothing.
	func profileDidOpen(_ id: String) {
		let volume = volume
		let locks = locks

		Task { [weak self] in
			try? await Task.sleep(for: .seconds(5))

			guard locks == self?.locks else {
				return
			}

			Task.detached(priority: .utility) {
				volume.safetyCopyDidOpen(profile: id)
			}
		}
	}

	// A deleted profile's data: its links, and its folder in the volume. `removed`
	// runs once it is all gone: until then the deletion's record stays, so a quit
	// on the way leaves no folder that nothing names.
	func remove(profile id: String, _ removed: @escaping () -> Void) {
		let volume = volume

		Task {
			let gone = await Task.detached(priority: .utility) { () -> Bool in
				do {
					try volume.remove(profile: id)
					return true
				} catch {
					Log.security.error("Could not remove a deleted profile's data: \(String(describing: error), privacy: .private)")
					return false
				}
			}.value

			if gone {
				removed()
			}
		}
	}

	// After the engine has shut down, when nothing has the volume's files
	// open: the image detached, mounted or still locked. An attach still under way, the first launch's
	// making of the image say, is waited for first: quit in it, Pane left the
	// image attached once it finished, and before its watchdog started.
	func close() {
		// A lock's detach, still under way, or the device it left attached.
		if let left = Self.settled(closing) ?? nil, device == nil {
			device = left
		}

		if device == nil {
			// The launch's image only if it still holds this image: a volume refused
			// as not encrypted was detached then, and its number may be another
			// disk's by now.
			device = Self.settled(attaching)?.device
				?? (Self.settled(image)??.image).flatMap { volume.holds(device: $0) ? $0 : nil }
		}

		guard let device else {
			return
		}

		volume.safetyCopiesClosed()
		Self.detach(volume, device: device)
		self.device = nil
	}

	// The image detached by its device, with the engine stopped: the volume's
	// helpers given a moment (the network service keeps the cookies open), then
	// forced.
	private nonisolated static func detach(_ volume: DataVolume, device: String) {
		let started = Date()

		do {
			if !tryDetaching(volume, device: device, waits: [0, 50, 100, 200, 400, 800]) {
				try volume.detach(device: device, force: true)
			}

			endWatchdog()
			Log.security.notice("The data volume detached in \(Date().timeIntervalSince(started), format: .fixed(precision: 2)) s")
		} catch {
			Log.security.error("Could not detach the data volume: \(String(describing: error), privacy: .private)")
		}
	}

	private nonisolated static func tryDetaching(_ volume: DataVolume, device: String, waits: [Int]) -> Bool {
		for wait in waits {
			usleep(useconds_t(wait * 1000))

			if (try? volume.detach(device: device)) != nil {
				return true
			}
		}

		return false
	}

	// At a lock, with the engine running: never forced, which would pull the files
	// from under it, the first flush after that stopping the process (ADR 0116).
	// A profile's stores close a moment after its context does. 10 s, at most.
	private nonisolated static let lockDetachWaits = [0, 50, 100, 200, 400, 800] + Array(repeating: 500, count: 17)

	// Pane locked while open (ADR 0116): the vault starts over at once,
	// closed, for the PIN to open again, and an attach the PIN starts waits
	// for this lock's detach (attacher). Off the main thread, the detach waits
	// for an attach or move still under way, and for `closed`, which says
	// every profile began closing in the engine: its stores close a moment after.
	// A profile that would not close keeps its files open: the image stays
	// attached for the quit to detach once the engine has stopped (close), where
	// forcing it would pull the files from under the engine.
	func lock(once closed: Task<Bool, Never>) {
		locks += 1
		dataKey = nil
		phase = .closed
		failure = nil
		attachedAt = nil

		let volume = volume
		let known = device
		let attaching = attaching
		let image = image
		let finishing = finishing

		device = nil
		self.attaching = nil
		self.image = nil
		self.finishing = nil

		// A profile waiting for the attach opens no more.
		let waiters = attachWaiters
		attachWaiters = []
		waiters.forEach { $0(false) }

		closing = Task.detached(priority: .userInitiated) { [weak self] in
			await finishing?.value
			await self?.migrationsFinished()

			var device = known

			if device == nil, let attached = try? await attaching?.value {
				device = attached.device
			}

			if device == nil, let devices = await image?.value, volume.holds(device: devices.image) {
				device = devices.image
			}

			guard let device else {
				return nil
			}

			guard await closed.value else {
				return device
			}

			volume.safetyCopiesClosed()

			guard Self.tryDetaching(volume, device: device, waits: Self.lockDetachWaits) else {
				Log.security.error("Could not detach the data volume at a lock: Pane quits to close it")
				await MainActor.run { [weak self] in self?.controller?.quit() }
				return device
			}

			await MainActor.run { [weak self] in self?.controller?.volumeDidDetachAtLock() }
			return nil
		}
	}

	// A task's value, waited for on the main thread at quit, which the attaches
	// never need: 5 s at most, where the image's making takes 1.5. It spins the run
	// loop, since a lock's detach ends on the main actor.
	private nonisolated static func settled<Value: Sendable, Failure: Error>(_ task: Task<Value, Failure>?) -> Value? {
		guard let task else {
			return nil
		}

		let box = Settled<Value>()
		let done = DispatchSemaphore(value: 0)

		Task.detached {
			if case let .success(value) = await task.result {
				box.value = value
			}

			done.signal()
		}

		let deadline = Date().addingTimeInterval(5)

		while done.wait(timeout: .now()) == .timedOut, Date() < deadline {
			RunLoop.main.run(mode: .default, before: Date().addingTimeInterval(0.01))
		}

		return box.value
	}

	// The profiles' ids, once the model has them: what an open moves in.
	private var profileIDs: [String] {
		controller?.model.profiles.map(\.id) ?? []
	}

	private func becomeOpen() {
		phase = .open
		controller?.accessDidOpen()
	}

	private func moved(_ share: Double, locks: Int) {
		if locks == self.locks, case .moving = phase {
			phase = .moving(share)
		}
	}

	// Once mounted: moves an earlier Pane's plain folders in when `ids`
	// names them, and purges the profiles deleted while it was detached; the
	// plain copies are erased after, off the launch.
	private func finish(_ attaching: Task<Attached, Error>, moving ids: [String]?) {
		let volume = volume
		let dataDirectory = dataDirectory
		let caches = AppLock.cacheDirectory(for: dataDirectory)
		let deletions = ProfileDeletions(dataDirectory: dataDirectory)
		let locks = locks

		finishing = Task.detached(priority: .userInitiated) { [weak self] in
			do {
				let attached = try await attaching.value

				if let ids {
					var reported = 0.0

					try volume.moveIn(profiles: ids) { share in
						guard share - reported >= 0.01 || share == 1 else {
							return
						}

						reported = share
						Task { @MainActor in self?.moved(share, locks: locks) }
					}
				}

				for failure in deletions.purge(volume: volume) {
					Log.security.error("Could not remove a deleted profile's data: \(failure, privacy: .private)")
				}

				await MainActor.run { self?.didAttach(attached, moved: ids != nil, locks: locks) }

				// On its own: a lock waits for what this task does, and the volume
				// stays mounted behind the PIN until it ends.
				if ids != nil {
					Task.detached(priority: .utility) {
						for failure in PanicWipe.eraseStaged(dataDirectory: dataDirectory, cacheDirectory: caches) {
							Log.security.error("Could not erase a plain copy: \(failure.description, privacy: .private)")
						}
					}
				}
			} catch {
				Log.security.error("Could not attach the data volume: \(String(describing: error), privacy: .private)")
				await MainActor.run { self?.attachFailed(String(describing: error), locks: locks) }
			}
		}
	}

	private func didAttach(_ attached: Attached, moved: Bool, locks: Int) {
		// Kept even when a lock overtook the attach: the image is this Pane's.
		if let lock = attached.lock {
			dataLock = lock
		}

		// The lock overtook it: its detach has the device.
		guard locks == self.locks else {
			return
		}

		device = attached.device
		attachedAt = (attached.started, attached.ended)

		if moved {
			becomeOpen()
		}

		let waiters = attachWaiters
		attachWaiters = []
		waiters.forEach { $0(true) }

		// A profile a window asked for goes first: it asked just above.
		encryptRemainingProfiles(profileIDs)
	}

	// No profile's context opens: the window still shows the profiles and
	// their tabs.
	private func attachFailed(_ reason: String, locks: Int) {
		guard locks == self.locks else {
			return
		}

		failure = reason
		controller?.lock.volumeWontOpen()

		if case .moving = phase {
			becomeOpen()
		}

		let waiters = attachWaiters
		attachWaiters = []
		waiters.forEach { $0(false) }
	}
}
