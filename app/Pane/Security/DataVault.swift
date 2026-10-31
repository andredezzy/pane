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
// are in place (BrowserController.withContext). The first open after an
// earlier Pane moves its plain profile folders in, with the lock's panel
// showing the move (Paper 07.5).
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
	// Pane's own files' key, once the data key opened.
	@ObservationIgnored private(set) var filesKey: SymmetricKey?
	@ObservationIgnored private(set) var isAttached = false
	// Why the volume would not attach, for the log and the security scenario.
	@ObservationIgnored private(set) var failure: String?
	// The attached image's device, which detaches it whole.
	@ObservationIgnored private var device: String?
	// The image's attach at launch, its volume locked (attachImage).
	@ObservationIgnored private var image: Task<DataVolume.Devices?, Never>?
	// The data's lock, held from launch, or taken once the Pane that held it
	// let go: the image is this one's alone while it is (DataLock).
	@ObservationIgnored private var dataLock: DataLock?
	// When the attach started and ended, in milliseconds since 1970.
	@ObservationIgnored private(set) var attachedAt: (started: Double, ended: Double)?
	@ObservationIgnored private var attachWaiters: [(Bool) -> Void] = []
	// The key opened: the model reads the profiles.
	@ObservationIgnored var didOpen: (() -> Void)?
	// The profiles are read and their move, if any, done.
	@ObservationIgnored var didBecomeOpen: (() -> Void)?
	// The profiles' ids, once the model has them: what an open moves in.
	@ObservationIgnored var profileIDs: () -> [String] = { [] }
	@ObservationIgnored private let dataDirectory: URL

	init(dataDirectory: URL) {
		self.dataDirectory = dataDirectory
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
		filesKey = DataKey.filesKey(key)
		didOpen?()

		let ids = profileIDs()
		let moving = ids.contains(where: volume.hasPlainFolder)

		if moving {
			phase = .moving(0)
		} else {
			becomeOpen()
		}

		finish(attaching, moving: moving ? ids : nil)
	}

	// The device the attach returned, and when it started and ended, in
	// milliseconds since 1970: for the security scenario and benchmark-unlock.
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

			watch(volume, device: devices.image)
			return devices
		}
	}

	// The data's lock and the image's attach from main.swift, whose device
	// the quit detaches: neither, when another Pane held the data at launch.
	func adopt(_ image: Task<DataVolume.Devices?, Never>?, lock: DataLock?) {
		self.image = image
		dataLock = lock

		guard let image else {
			return
		}

		Task { [weak self] in
			if let devices = await image.value {
				self?.device = devices.image
			}
		}
	}

	// What attaches the volume once the key opens, off the main thread, from
	// wherever it opened: the main thread, busy at launch and as the lock
	// slides away, never holds it back.
	func attacher() -> @Sendable (SymmetricKey) -> Task<Attached, Error> {
		let volume = volume
		let image = image
		let locking = dataLock == nil ? dataDirectory : nil

		return { key in
			Task.detached(priority: .userInitiated) { try await Self.attach(volume, key: key, image: image, locking: locking) }
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
			watch(volume, device: devices.image)
		}

		try volume.open(devices, passphrase: passphrase)

		let ended = Date().timeIntervalSince1970 * 1000
		Log.security.notice("The data volume opened in \((ended - started) / 1000, format: .fixed(precision: 2)) s")

		return Attached(device: devices.image, started: started, ended: ended, lock: lock)
	}

	private nonisolated static func watch(_ volume: DataVolume, device: String) {
		guard let helper = Bundle.main.url(forAuxiliaryExecutable: "pane-volume-watch") else {
			Log.security.error("No pane-volume-watch in the bundle: a crash leaves the volume attached")
			return
		}

		do {
			try volume.watch(device: device, helper: helper)
		} catch {
			Log.security.error("Could not start the volume's watchdog: \(String(describing: error), privacy: .private)")
		}
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
	func prepare(profile id: String, _ done: @escaping (Bool) -> Void) {
		let volume = volume

		Task {
			let prepared = await Task.detached(priority: .userInitiated) { () -> Bool in
				do {
					try volume.prepare(profile: id)
					return true
				} catch {
					Log.security.error("Could not prepare a profile's folders: \(String(describing: error), privacy: .private)")
					return false
				}
			}.value

			done(prepared)
		}
	}

	// A deleted profile's data: its links, and its folder in the volume.
	func remove(profile id: String) {
		let volume = volume

		Task.detached(priority: .utility) {
			do {
				try volume.remove(profile: id)
			} catch {
				Log.security.error("Could not remove a deleted profile's data: \(String(describing: error), privacy: .private)")
			}
		}
	}

	// After the engine has shut down, when nothing has the volume's files
	// open: the image detached, mounted or still locked, and compacted when it
	// keeps much unused space.
	func close() {
		guard let device else {
			return
		}

		let used = isAttached ? volume.usedBytes : nil
		let started = Date()

		// The engine's helpers end a moment after it (the network service
		// keeps the cookies open): the detach waits for them, then forces.
		var detached = false

		for delay in [0, 50, 100, 200, 400, 800] {
			usleep(useconds_t(delay * 1000))

			if (try? volume.detach(device: device)) != nil {
				detached = true
				break
			}
		}

		do {
			if !detached {
				try volume.detach(device: device, force: true)
			}

			isAttached = false
			Log.security.notice("The data volume detached in \(Date().timeIntervalSince(started), format: .fixed(precision: 2)) s")

			if let used {
				try volume.compact(used: used)
			}
		} catch {
			Log.security.error("Could not detach the data volume: \(String(describing: error), privacy: .private)")
		}
	}

	private func becomeOpen() {
		phase = .open
		didBecomeOpen?()
	}

	private func moved(_ share: Double) {
		if case .moving = phase {
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

		Task.detached(priority: .userInitiated) { [weak self] in
			do {
				let attached = try await attaching.value

				if let ids {
					var reported = 0.0

					try volume.moveIn(profiles: ids) { share in
						guard share - reported >= 0.01 || share == 1 else {
							return
						}

						reported = share
						Task { @MainActor in self?.moved(share) }
					}
				}

				for failure in deletions.purge(volume: volume) {
					Log.security.error("Could not remove a deleted profile's data: \(failure, privacy: .private)")
				}

				await MainActor.run { self?.didAttach(attached, moved: ids != nil) }

				if ids != nil {
					for failure in PanicWipe.eraseStaged(dataDirectory: dataDirectory, cacheDirectory: caches) {
						Log.security.error("Could not erase a plain copy: \(failure.description, privacy: .private)")
					}
				}
			} catch {
				Log.security.error("Could not attach the data volume: \(String(describing: error), privacy: .private)")
				await MainActor.run { self?.attachFailed(String(describing: error)) }
			}
		}
	}

	private func didAttach(_ attached: Attached, moved: Bool) {
		isAttached = true
		device = attached.device
		attachedAt = (attached.started, attached.ended)

		if let lock = attached.lock {
			dataLock = lock
		}

		if moved {
			becomeOpen()
		}

		let waiters = attachWaiters
		attachWaiters = []
		waiters.forEach { $0(true) }
	}

	// No profile's context opens: the window still shows the profiles and
	// their tabs.
	private func attachFailed(_ reason: String) {
		failure = reason

		if case .moving = phase {
			becomeOpen()
		}

		let waiters = attachWaiters
		attachWaiters = []
		waiters.forEach { $0(false) }
	}
}
