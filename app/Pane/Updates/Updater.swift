import AppKit
import CryptoKit
import Observation
import PaneKit

// The update check and download (ADR 0002; apps/veil updater.ts and
// update-store.ts): the latest GitHub Release at launch and daily, with
// retries after a failure; one click downloads its dmg to Downloads and opens
// it, and the user drags Pane to Applications. Never a modal, never a forced
// restart (DESIGN.md: Updates).
@MainActor
@Observable
final class Updater {
	enum Status: String {
		case idle
		case checking
		case available
		case downloading
		case downloaded
	}

	private(set) var status = Status.idle
	private(set) var available: AvailableUpdate?
	private(set) var lastCheckedAt: Date?
	// 0–1 while a download runs.
	private(set) var downloadProgress: Double?
	// Whole seconds left, once the rate settles.
	private(set) var downloadETA: Int?
	// The version whose pill was dismissed this launch.
	private(set) var dismissedVersion: String?

	// Scenarios point these at their own release, folder, and no opening.
	@ObservationIgnored private let releasesURL = ProcessInfo.processInfo.environment["PANE_UPDATE_URL"]
		.flatMap(URL.init(string:)) ?? Updates.releasesURL
	@ObservationIgnored private let downloadsDirectory = ProcessInfo.processInfo.environment["PANE_UPDATE_DOWNLOADS"]
		.map { URL(fileURLWithPath: $0, isDirectory: true) }
		?? FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask)[0]
	@ObservationIgnored private let opensDownload = ProcessInfo.processInfo.environment["PANE_UPDATE_OPEN"] != "0"
	@ObservationIgnored private let currentVersion: String
	@ObservationIgnored private var scheduled: Task<Void, Never>?
	@ObservationIgnored private var download: DmgDownload?
	@ObservationIgnored private var isChecking = false
	// The dmg downloaded this launch, which the pill opens again.
	@ObservationIgnored private var downloaded: (version: String, file: URL)?

	init(currentVersion: String) {
		self.currentVersion = currentVersion
	}

	// Checks now, then a day after each completed check, or 5, 15, then 60
	// minutes after a failed one.
	func schedule() {
		scheduled?.cancel()
		scheduled = Task { [weak self] in
			var retry = 0

			while !Task.isCancelled {
				guard let self else {
					return
				}

				let delay: TimeInterval

				if await check() {
					retry = 0
					delay = Updates.checkInterval
				} else {
					delay = Updates.retryDelays[min(retry, Updates.retryDelays.count - 1)]
					retry += 1
				}

				try? await Task.sleep(for: .seconds(delay))
			}
		}
	}

	// Settings' Check for updates, the app menu's, and the schedule. Never
	// throws: offline, rate-limited, or a bad answer, what was on offer
	// stays. Returns whether the check completed.
	@discardableResult
	func check() async -> Bool {
		guard !isChecking, status != .downloading else {
			return true
		}

		isChecking = true
		defer { isChecking = false }

		// Only a check with nothing on offer shows (Settings' Checking…): an
		// offer stays up while its check runs, so the pill never blinks.
		if status == .idle {
			status = .checking
		}

		let settle = Task { try? await Task.sleep(for: .seconds(Updates.minimumChecking)) }

		do {
			var request = URLRequest(url: releasesURL)
			request.setValue(Updates.userAgent, forHTTPHeaderField: "User-Agent")
			let (data, response) = try await URLSession.shared.data(for: request)

			guard (response as? HTTPURLResponse)?.statusCode == 200 else {
				throw URLError(.badServerResponse)
			}

			let update = try Updates.available(inRelease: data, currentVersion: currentVersion, from: releasesURL)
			await settle.value
			lastCheckedAt = Date()
			offer(update)

			return true
		} catch {
			NSLog("Pane: the update check failed: %@", String(describing: error))
			await settle.value

			if status == .checking {
				status = .idle
			}

			return false
		}
	}

	// What a completed check found. A download that started meanwhile runs
	// on, and the version already downloaded stays downloaded.
	private func offer(_ update: AvailableUpdate?) {
		guard status != .downloading else {
			return
		}

		available = update

		if let update {
			status = update.version == downloaded?.version ? .downloaded : .available
		} else {
			status = .idle
		}
	}

	// The pill's dismiss: hidden for this version until relaunch.
	func dismissPill() {
		dismissedVersion = available?.version
	}

	// The pill's click and Settings' Download update: downloads the dmg, or
	// opens it again once downloaded. A failure returns to available, so the
	// user can try again from the same place.
	func downloadUpdate() {
		if status == .downloaded, let file = downloaded?.file, FileManager.default.fileExists(atPath: file.path) {
			open(file)

			return
		}

		guard status == .available || status == .downloaded, let available else {
			return
		}

		status = .downloading
		downloadProgress = 0
		downloadETA = nil

		let destination = downloadsDirectory.appending(path: available.dmgURL.lastPathComponent)

		download = DmgDownload(from: available.dmgURL, to: destination, sha256: available.sha256) { [weak self] progress, eta in
			self?.downloadProgress = progress
			self?.downloadETA = eta
		} done: { [weak self] result in
			guard let self else {
				return
			}

			download = nil
			downloadProgress = nil
			downloadETA = nil

			switch result {
			case let .success(file):
				downloaded = (available.version, file)
				status = .downloaded
				open(file)
			case let .failure(error):
				NSLog("Pane: the update download failed: %@", String(describing: error))
				status = .available
			}
		}
	}

	private func open(_ file: URL) {
		if opensDownload {
			NSWorkspace.shared.open(file)
		}
	}

	// The status line the pill and the lock screen show.
	var label: String {
		switch status {
		case .downloading: "Downloading update"
		case .downloaded: "Downloaded, quit to install"
		default: "Update available"
		}
	}

	// The pill shows while an update is on offer and not dismissed.
	var showsPill: Bool {
		[.available, .downloading, .downloaded].contains(status) && available != nil
			&& available?.version != dismissedVersion
	}
}

// One dmg download, with its progress and time left, into the downloads
// folder (a name already taken there gets " (1)" and so on, as Chrome does).
// Given the release's SHA-256, the dmg must match it, and then leaves
// quarantine (ADR 0090): Pane quarantines every file it writes, and macOS
// will not open a quarantined copy of an app that is not notarized.
private final class DmgDownload: NSObject, URLSessionDownloadDelegate, @unchecked Sendable {
	private struct DigestMismatch: Error {
		let expected: String
		let actual: String
	}

	private let destination: URL
	private let sha256: String?
	private let progress: @MainActor (Double, Int?) -> Void
	private let done: @MainActor (Result<URL, Error>) -> Void
	private var eta = DownloadETA()
	private var session: URLSession!

	init(
		from url: URL,
		to destination: URL,
		sha256: String?,
		progress: @escaping @MainActor (Double, Int?) -> Void,
		done: @escaping @MainActor (Result<URL, Error>) -> Void
	) {
		self.destination = destination
		self.sha256 = sha256
		self.progress = progress
		self.done = done
		super.init()

		session = URLSession(configuration: .default, delegate: self, delegateQueue: .main)
		var request = URLRequest(url: url)
		request.setValue(Updates.userAgent, forHTTPHeaderField: "User-Agent")
		session.downloadTask(with: request).resume()
	}

	func urlSession(_: URLSession, downloadTask _: URLSessionDownloadTask, didWriteData _: Int64, totalBytesWritten: Int64, totalBytesExpectedToWrite: Int64) {
		guard totalBytesExpectedToWrite > 0 else {
			return
		}

		let fraction = Double(totalBytesWritten) / Double(totalBytesExpectedToWrite)
		let seconds = eta.update(received: totalBytesWritten, total: totalBytesExpectedToWrite, now: Date())

		MainActor.assumeIsolated { progress(fraction, seconds) }
	}

	func urlSession(_: URLSession, downloadTask: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {
		session.finishTasksAndInvalidate()

		if let status = (downloadTask.response as? HTTPURLResponse)?.statusCode, status != 200 {
			MainActor.assumeIsolated { done(.failure(URLError(.badServerResponse))) }

			return
		}

		let file: URL

		do {
			file = try moveToFreeName(location)
		} catch {
			MainActor.assumeIsolated { done(.failure(error)) }

			return
		}

		// The digest reads the whole dmg: off the main thread.
		Task.detached(priority: .userInitiated) { [self] in
			let result = Result { try Self.verify(file, sha256: sha256) }
			await MainActor.run { done(result) }
		}
	}

	// A dmg that differs from the release's SHA-256 is deleted; a matching
	// one leaves quarantine. Without a digest it stays quarantined, and
	// Gatekeeper asks before the app inside first opens.
	private static func verify(_ file: URL, sha256: String?) throws -> URL {
		guard let sha256 else {
			return file
		}

		let handle = try FileHandle(forReadingFrom: file)
		defer { try? handle.close() }

		var hasher = SHA256()

		while let chunk = try handle.read(upToCount: 1 << 20), !chunk.isEmpty {
			hasher.update(data: chunk)
		}

		let actual = hasher.finalize().map { String(format: "%02x", $0) }.joined()

		guard actual == sha256 else {
			try? FileManager.default.removeItem(at: file)

			throw DigestMismatch(expected: sha256, actual: actual)
		}

		var values = URLResourceValues()
		values.quarantineProperties = nil
		var verified = file
		try verified.setResourceValues(values)

		return file
	}

	func urlSession(_: URLSession, task _: URLSessionTask, didCompleteWithError error: Error?) {
		guard let error else {
			return
		}

		session.finishTasksAndInvalidate()
		MainActor.assumeIsolated { done(.failure(error)) }
	}

	private func moveToFreeName(_ location: URL) throws -> URL {
		let fileManager = FileManager.default
		try fileManager.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)

		let stem = destination.deletingPathExtension().lastPathComponent
		let ext = destination.pathExtension
		var candidate = destination
		var number = 1

		while fileManager.fileExists(atPath: candidate.path) {
			candidate = destination.deletingLastPathComponent().appending(path: "\(stem) (\(number)).\(ext)")
			number += 1
		}

		try fileManager.moveItem(at: location, to: candidate)

		return candidate
	}
}
