import CryptoKit
import Foundation
import PaneKit
import os

// One dmg download, with its progress and time left, into the downloads
// folder (a name already taken there gets " (1)" and so on, as Chrome does).
// A connection that crawls is left for another, from where it got to
// (DownloadPace). Given the release's SHA-256, the dmg must match it, and then
// leaves quarantine (ADR 0002): Pane quarantines every file it writes, and
// macOS will not open a quarantined copy of an app that is not notarized.
final class DmgDownload: NSObject, URLSessionDownloadDelegate, @unchecked Sendable {
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
		let lastSession = session
		session = URLSession(configuration: .ephemeral, delegate: self, delegateQueue: .main)
		lastSession?.finishTasksAndInvalidate()

		let task = resumeData.map(session.downloadTask(withResumeData:)) ?? session.downloadTask(with: request)
		self.task = task
		isResumed = resumeData != nil
		isSwitching = false
		hasBytes = false

		self.resumeData = resumeData

		if resumeData == nil {
			received = 0
			eta = DownloadETA()
		} else {
			eta.restartRate()
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

		// A resumed task cancelled before its answer gives none: where the last
		// one stopped still stands.
		start(resuming: isRefused ? nil : newData ?? resumeData)
	}

	// The server refused to go on from there (GitHub's signed link expires):
	// from the start, once.
	private func startOver() {
		Log.updates.notice("The download could not resume, and starts over")
		isRefused = true
		start()
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

			finish()
			MainActor.assumeIsolated { done(.failure(URLError(.badServerResponse))) }

			return
		}

		finish()

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
				self?.start(resuming: data)
			}

			return
		}

		// Refused rather than dropped: from the start, once.
		if isResumed, drops == 0 {
			startOver()

			return
		}

		finish()
		MainActor.assumeIsolated { done(.failure(error)) }
	}

	private func finish() {
		isFinished = true
		session.finishTasksAndInvalidate()
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
