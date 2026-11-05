import AppKit
import CryptoKit
import Observation
import PaneKit
import os

// The update check, download, and install (ADR 0002, ADR 0092; apps/veil
// updater.ts and update-store.ts): the latest GitHub Release at launch and
// every six hours (ADR 0002), with retries after a failure; one click
// downloads its dmg to Downloads, and once it checks out Pane stages the new
// version beside itself. Restart to update quits into it, and any quit
// installs it. When Pane cannot replace itself, the dmg opens and the user
// drags Pane to Applications. Never a modal, never a forced restart
// (DESIGN.md: Updates).
@MainActor
@Observable
final class Updater {
	// Ephemeral, as the download's: no cache or cookie of GitHub's answers
	// in ~/Library, outside the data volume (ADR 0113).
	private static let session = URLSession(configuration: .ephemeral)

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
	// The downloaded version's Pane, staged beside this one to swap in at quit.
	private(set) var staged: URL?

	// Scenarios point these at their own release, folder, and no opening.
	@ObservationIgnored private let releasesURL = TestHooks.environment("PANE_UPDATE_URL")
		.flatMap(URL.init(string:)) ?? Updates.releasesURL
	@ObservationIgnored private let downloadsDirectory = TestHooks.environment("PANE_UPDATE_DOWNLOADS")
		.map { URL(fileURLWithPath: $0, isDirectory: true) }
		?? FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask)[0]
	@ObservationIgnored private let opensDownload = TestHooks.environment("PANE_UPDATE_OPEN") != "0"
	// Nil where Pane cannot replace itself, and in scenarios that turn it off.
	@ObservationIgnored private let installer = TestHooks.environment("PANE_UPDATE_INSTALL") == "0" ? nil : UpdateInstaller()
	// Restart to update: the quit opens the new Pane too.
	@ObservationIgnored private var relaunches = false
	@ObservationIgnored private let currentVersion: String
	@ObservationIgnored private var scheduled: Task<Void, Never>?
	@ObservationIgnored private var download: DmgDownload?
	@ObservationIgnored private var isChecking = false
	// The dmg downloaded this launch, which the pill opens again.
	@ObservationIgnored private var downloaded: (version: String, file: URL)?

	init(currentVersion: String) {
		self.currentVersion = currentVersion

		// What an earlier install left beside the app, off the launch's path.
		if let installer {
			Task.detached(priority: .utility) { installer.removeLeftovers() }
		}
	}

	// Checks now, then six hours after each completed check, or 5, 15, then
	// 60 minutes after a failed one. A tenth of the wait is slack, so the
	// system can batch the wake with others (Apple's energy guide).
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

				try? await Task.sleep(for: .seconds(delay), tolerance: .seconds(delay / 10))
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
			let (data, response) = try await Self.session.data(for: request)

			guard (response as? HTTPURLResponse)?.statusCode == 200 else {
				throw URLError(.badServerResponse)
			}

			let update = try Updates.available(inRelease: data, currentVersion: currentVersion, from: releasesURL)
			await settle.value
			lastCheckedAt = Date()
			offer(update)

			return true
		} catch {
			Log.updates.error("The update check failed: \(String(describing: error), privacy: .private)")
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

	// The pill's click and Settings' Download update: downloads the dmg;
	// once downloaded, restarts into it, or, where Pane can't install it
	// itself, quits for the user to (quitAndInstall). A failure returns to
	// available, so the user can try again from the same place.
	func downloadUpdate() {
		if status == .downloaded, staged != nil {
			restartToUpdate()

			return
		}

		if status == .downloaded, let file = downloaded?.file, FileManager.default.fileExists(atPath: file.path) {
			quitAndInstall(file)

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
			downloadETA = nil

			switch result {
			case let .success(file):
				downloaded = (available.version, file)
				stage(file, version: available.version)
			case let .failure(error):
				Log.updates.error("The update download failed: \(String(describing: error), privacy: .private)")
				downloadProgress = nil
				status = .available
			}
		}
	}

	// The checked dmg's Pane, copied beside this one (ADR 0092) while the pill
	// holds its full bar; without an installer, or when a step fails, the dmg
	// opens for the user to drag.
	private func stage(_ file: URL, version: String) {
		guard let installer else {
			downloadProgress = nil
			status = .downloaded
			open(file)

			return
		}

		downloadProgress = 1

		Task.detached(priority: .userInitiated) { [weak self] in
			let result = Result { try installer.stage(file, version: version) }

			await MainActor.run {
				guard let self else {
					return
				}

				self.downloadProgress = nil
				self.status = .downloaded

				switch result {
				case let .success(url):
					self.staged = url
				case let .failure(error):
					Log.updates.error("The update did not stage: \(String(describing: error), privacy: .private)")
					self.open(file)
				}
			}
		}
	}

	// The pill's, Settings', and the lock screen's Restart to update: quits,
	// and the quit swaps the staged Pane in and opens it.
	func restartToUpdate() {
		guard status == .downloaded, staged != nil else {
			return
		}

		relaunches = true
		NSApp.terminate(nil)
	}

	// An update Pane can't install itself (a copy of another identifier, a
	// Pane outside a folder it can write; Paper 06.3.1): its installer comes
	// forward, and Pane quits, as the copy in Applications can only be
	// replaced once it has. The user drags the new one in.
	private func quitAndInstall(_ file: URL) {
		open(file)
		NSApp.terminate(nil)
	}

	// At every quit (AppDelegate), a staged update swaps in. A swap macOS
	// refuses leaves this app as it was, and the dmg opens instead.
	func installAtQuit() {
		guard let installer, let staged else {
			return
		}

		do {
			try installer.install(staged, dmg: downloaded?.file, relaunch: relaunches)
		} catch {
			Log.updates.error("The update did not install: \(String(describing: error), privacy: .private)")
			try? FileManager.default.removeItem(at: staged)

			if let file = downloaded?.file {
				open(file)
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
		case .downloaded: staged == nil ? "Quit and install" : "Restart to update"
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
// A connection that crawls is left for another, from where it got to
// (DownloadPace). Given the release's SHA-256, the dmg must match it, and then
// leaves quarantine (ADR 0002): Pane quarantines every file it writes, and
// macOS will not open a quarantined copy of an app that is not notarized.
private final class DmgDownload: NSObject, URLSessionDownloadDelegate, @unchecked Sendable {
	private struct DigestMismatch: Error {
		let expected: String
		let actual: String
	}

	private let request: URLRequest
	private let destination: URL
	private let sha256: String?
	private let progress: @MainActor (Double, Int?) -> Void
	private let done: @MainActor (Result<URL, Error>) -> Void
	private var eta = DownloadETA()
	private var pace = DownloadPace(now: Date())
	private var session: URLSession!
	private var task: URLSessionDownloadTask?
	private var received: Int64 = 0
	// The connection has brought something, so its window runs from there.
	private var hasBytes = false
	// The task began from resume data, which the server may refuse.
	private var isResumed = false
	// The server refused to go on from where a connection left off: it is not
	// asked again.
	private var isRefused = false
	// Where the last connection left off, if the next never got far enough to
	// say.
	private var resumeData: Data?
	// Connections dropped in a row with nothing brought since.
	private var drops = 0
	// The task is being cancelled for another connection.
	private var isSwitching = false
	private var isFinished = false

	init(
		from url: URL,
		to destination: URL,
		sha256: String?,
		progress: @escaping @MainActor (Double, Int?) -> Void,
		done: @escaping @MainActor (Result<URL, Error>) -> Void
	) {
		var request = URLRequest(url: url)
		request.setValue(Updates.userAgent, forHTTPHeaderField: "User-Agent")
		self.request = request
		self.destination = destination
		self.sha256 = sha256
		self.progress = progress
		self.done = done
		super.init()

		start()
	}

	// On a session of its own: a task on the last one would share its connection.
	private func start(resuming resumeData: Data? = nil) {
		session = URLSession(configuration: .ephemeral, delegate: self, delegateQueue: .main)

		let task = resumeData.map(session.downloadTask(withResumeData:)) ?? session.downloadTask(with: request)
		self.task = task
		isResumed = resumeData != nil
		isSwitching = false
		hasBytes = false

		self.resumeData = resumeData

		if resumeData == nil {
			received = 0
			eta = DownloadETA()
		}

		pace.begin(received: received, now: Date())
		task.resume()
		judgeLater(task)
	}

	private func judgeLater(_ task: URLSessionDownloadTask, after delay: TimeInterval? = nil) {
		DispatchQueue.main.asyncAfter(deadline: .now() + (delay ?? pace.window)) { [weak self] in
			self?.judge(task)
		}
	}

	private func judge(_ judged: URLSessionDownloadTask) {
		guard judged === task, !isFinished else {
			return
		}

		// Its first bytes came since this was set, and the window runs from them.
		let timeLeft = pace.timeLeft(now: Date())

		guard timeLeft <= 0 else {
			judgeLater(judged, after: timeLeft)

			return
		}

		guard pace.shouldSwitch(received: received, now: Date(), canLeave: canLeave(judged)) else {
			judgeLater(judged)

			return
		}

		Log.updates.notice("The download left a slow connection \(self.received) bytes in")
		isSwitching = true

		judged.cancel { [weak self] resumeData in
			DispatchQueue.main.async {
				self?.switchConnection(from: judged, resuming: resumeData)
			}
		}
	}

	// A connection that cannot resume gives up what it brought, so it is left
	// only while that is little: nothing yet, or under a twentieth of the file.
	// A resumed one with no answer yet gives up nothing: where the last one
	// stopped still stands.
	private func canLeave(_ task: URLSessionDownloadTask) -> Bool {
		let response = task.response as? HTTPURLResponse
		let validated = response?.value(forHTTPHeaderField: "ETag") != nil || response?.value(forHTTPHeaderField: "Last-Modified") != nil
		let offersRanges = response?.statusCode == 206 || response?.value(forHTTPHeaderField: "Accept-Ranges") == "bytes"
		let canResume = !isRefused && offersRanges && validated

		return (isResumed && response == nil) || canResume || received == 0 || received * 20 < task.countOfBytesExpectedToReceive
	}

	private func switchConnection(from old: URLSessionDownloadTask, resuming newData: Data?) {
		guard old === task, !isFinished else {
			return
		}

		let oldSession = session
		// A resumed task cancelled before its answer gives none: where the last
		// one stopped still stands.
		start(resuming: isRefused ? nil : newData ?? resumeData)
		oldSession?.finishTasksAndInvalidate()
	}

	// The server refused to go on from there (GitHub's signed link expires):
	// from the start, once.
	private func startOver() {
		Log.updates.notice("The download could not resume, and starts over")
		isRefused = true
		let oldSession = session
		start()
		oldSession?.finishTasksAndInvalidate()
	}

	func urlSession(_: URLSession, downloadTask: URLSessionDownloadTask, didResumeAtOffset fileOffset: Int64, expectedTotalBytes _: Int64) {
		guard downloadTask === task else {
			return
		}

		received = fileOffset
		hasBytes = true
		pace.begin(received: fileOffset, now: Date())
	}

	func urlSession(_: URLSession, downloadTask: URLSessionDownloadTask, didWriteData bytesWritten: Int64, totalBytesWritten: Int64, totalBytesExpectedToWrite: Int64) {
		guard downloadTask === task else {
			return
		}

		if !hasBytes {
			hasBytes = true
			pace.begin(received: totalBytesWritten - bytesWritten, now: Date())
		}

		received = totalBytesWritten
		drops = 0

		guard totalBytesExpectedToWrite > 0 else {
			return
		}

		let fraction = Double(totalBytesWritten) / Double(totalBytesExpectedToWrite)
		let seconds = eta.update(received: totalBytesWritten, total: totalBytesExpectedToWrite, now: Date())

		MainActor.assumeIsolated { progress(fraction, seconds) }
	}

	func urlSession(_: URLSession, downloadTask: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {
		guard downloadTask === task else {
			return
		}

		// 206 is a resumed download's last part, in a file whole by now.
		if let status = (downloadTask.response as? HTTPURLResponse)?.statusCode, ![200, 206].contains(status) {
			if isResumed {
				startOver()

				return
			}

			isFinished = true
			session.finishTasksAndInvalidate()
			MainActor.assumeIsolated { done(.failure(URLError(.badServerResponse))) }

			return
		}

		isFinished = true
		session.finishTasksAndInvalidate()

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

	func urlSession(_: URLSession, task completed: URLSessionTask, didCompleteWithError error: Error?) {
		guard let error, completed === task, !isSwitching, !isFinished else {
			return
		}

		// A connection that dropped goes on from where it got to, after 2, 4,
		// then 8 s in a row with nothing brought: long enough for a Wi-Fi switch,
		// which would fail each at once.
		if let data = (error as NSError).userInfo[NSURLSessionDownloadTaskResumeData] as? Data, drops < 3 {
			drops += 1
			task = nil

			DispatchQueue.main.asyncAfter(deadline: .now() + Double(1 << drops)) { [weak self] in
				guard let self else {
					return
				}

				let oldSession = session
				start(resuming: data)
				oldSession?.finishTasksAndInvalidate()
			}

			return
		}

		// Refused rather than dropped: from the start, once.
		if isResumed, drops == 0 {
			startOver()

			return
		}

		isFinished = true
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
