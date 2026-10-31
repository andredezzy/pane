import Foundation

// Held by the one Pane that runs on a data root (ADR 0113): the image's
// attach, its watchdog, and its detach are that Pane's alone. Another one
// started on the same data, which hands its URLs to the first through the
// engine's singleton and quits, finds it held and leaves the image alone;
// one started while the last quits waits for it. A lock on a file, which the
// system lets go of with its process, a crash included, and which the
// processes Pane starts do not inherit (the update's relaunch, the
// watchdog).
public final class DataLock: Sendable {
	private let descriptor: Int32

	// Nil while another process holds it.
	public init?(dataDirectory: URL) {
		try? FileManager.default.createDirectory(at: dataDirectory, withIntermediateDirectories: true)
		let descriptor = open(dataDirectory.appending(path: "Pane.lock").path, O_RDWR | O_CREAT | O_CLOEXEC, 0o600)

		guard descriptor >= 0 else {
			return nil
		}

		guard flock(descriptor, LOCK_EX | LOCK_NB) == 0 else {
			close(descriptor)
			return nil
		}

		self.descriptor = descriptor
	}

	// Once the process that holds it lets go, which a Pane quitting does
	// after it detaches the image. Nil past the timeout.
	public static func wait(dataDirectory: URL, timeout: TimeInterval) -> DataLock? {
		let deadline = Date().addingTimeInterval(timeout)

		while true {
			if let lock = DataLock(dataDirectory: dataDirectory) {
				return lock
			}

			if Date() > deadline {
				return nil
			}

			usleep(50_000)
		}
	}

	deinit {
		close(descriptor)
	}
}
