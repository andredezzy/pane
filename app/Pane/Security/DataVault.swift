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
// are in place (BrowserController.withContext).
@MainActor
@Observable
final class DataVault {
	enum Phase: Equatable {
		// Nothing of the profiles is read yet.
		case closed
		case open
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
	// Locks so far: an attach a lock overtook reports to no one.
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

	// The key opened, and `attaching`, started where it opened, attaches the
	// volume.
	func open(_ key: SymmetricKey, attaching: Task<Attached, Error>) {
		dataKey = key
		self.attaching = attaching
		controller?.dataKeyDidOpen()
		phase = .open
		controller?.accessDidOpen()
		finish(attaching)
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

		try volume.open(devices, passphrase: passphrase)

		let ended = Date().timeIntervalSince1970 * 1000
		Log.security.notice("The data volume opened in \((ended - started) / 1000, format: .fixed(precision: 2)) s")

		return Attached(device: devices.image, started: started, ended: ended, lock: lock)
	}

	// One watchdog for this Pane's life: it finds the image by its path.
	private nonisolated static let watching = OSAllocatedUnfairLock(initialState: false)

	private nonisolated static func watch(_ volume: DataVolume) {
		guard !watching.withLock({ started in
			defer { started = true }
			return started
		}) else {
			return
		}

		guard let helper = Bundle.main.url(forAuxiliaryExecutable: "pane-volume-watch") else {
			Log.security.error("No pane-volume-watch in the bundle: a crash leaves the volume attached")
			return
		}

		do {
			try volume.watch(helper: helper)
		} catch {
			Log.security.error("Could not start the volume's watchdog: \(String(describing: error), privacy: .private)")
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

	// The profile's folders in the volume and its links, off the main thread.
	func prepare(profile id: String, savesHistory: Bool, _ done: @escaping (Bool) -> Void) {
		let volume = volume

		Task {
			done(await Task.detached(priority: .userInitiated) { () -> Bool in
				do {
					try volume.prepare(profile: id, savesHistory: savesHistory)
					return true
				} catch {
					Log.security.error("Could not prepare a profile's folders: \(String(describing: error), privacy: .private)")
					return false
				}
			}.value)
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
			device = Self.settled(attaching)?.device ?? Self.settled(image)??.image
		}

		guard let device else {
			return
		}

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
	// for an attach still under way, and for `closed`, which says
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

			var device = known

			if device == nil, let attached = try? await attaching?.value {
				device = attached.device
			}

			if device == nil, let devices = await image?.value {
				device = devices.image
			}

			guard let device else {
				return nil
			}

			guard await closed.value else {
				return device
			}

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

	// Once mounted: purges the profiles deleted while it was detached.
	private func finish(_ attaching: Task<Attached, Error>) {
		let deletions = ProfileDeletions(dataDirectory: dataDirectory)
		let volume = volume
		let locks = locks

		finishing = Task.detached(priority: .userInitiated) { [weak self] in
			do {
				let attached = try await attaching.value

				for failure in deletions.purge(volume: volume) {
					Log.security.error("Could not remove a deleted profile's data: \(failure, privacy: .private)")
				}

				await MainActor.run { self?.didAttach(attached, locks: locks) }
			} catch {
				Log.security.error("Could not attach the data volume: \(String(describing: error), privacy: .private)")
				await MainActor.run { self?.attachFailed(String(describing: error), locks: locks) }
			}
		}
	}

	private func didAttach(_ attached: Attached, locks: Int) {
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

		let waiters = attachWaiters
		attachWaiters = []
		waiters.forEach { $0(true) }
	}

	// No profile's context opens: the window still shows the profiles and
	// their tabs.
	private func attachFailed(_ reason: String, locks: Int) {
		guard locks == self.locks else {
			return
		}

		failure = reason
		controller?.lock.volumeWontOpen()

		let waiters = attachWaiters
		attachWaiters = []
		waiters.forEach { $0(false) }
	}
}
