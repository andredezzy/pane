import Foundation
import PaneKit
import os

// The panic wipe's process around PanicWipe's erase (ADR 0042): the wiped
// instance's helpers, the instance it starts, and what that one does as it starts.
@MainActor
enum PanicWipeRun {
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
	static func atLaunch(_ dataDirectory: URL) {
		let caches = AppLock.cacheDirectory(for: dataDirectory)
		let wiped = CommandLine.arguments.first { $0.hasPrefix(wipeSwitch) }
			.map { $0.dropFirst(wipeSwitch.count).split(separator: ",").compactMap { pid_t($0) } }

		if let wiped {
			let deadline = Date().addingTimeInterval(30)

			while wiped.contains(where: { kill($0, 0) == 0 }), Date() < deadline {
				usleep(50_000)
			}
		}

		if wiped != nil || isWipeDue(dataDirectory) {
			// The wiped instance's volume, which its watchdog went down with:
			// its key is gone, and it moves aside detached.
			DataVolume(dataDirectory: dataDirectory, cacheDirectory: caches).detachLeftovers()

			log(PanicWipe.stage(dataDirectory: dataDirectory, cacheDirectory: caches))
		}

		guard !PanicWipe.staged(dataDirectory: dataDirectory, cacheDirectory: caches).isEmpty else {
			return
		}

		isErasing = true

		let thread = Thread {
			let started = Date()
			let failures = PanicWipe.eraseStaged(dataDirectory: dataDirectory, cacheDirectory: caches)
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

		guard let state = try? file.load() else {
			return false
		}

		return state.pin != nil && state.failedAttempts >= SecurityState.maxAttempts && state.key == nil
	}

	static func log(_ failures: [PanicWipe.Failure]) {
		for failure in failures {
			Log.security.error("The panic wipe could not erase \(failure.description, privacy: .private)")
		}
	}

	// The engine's helpers: renderers, the GPU and network services, and
	// Crashpad's handler.
	static func childProcesses() -> [pid_t] {
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
	static func relaunch(after processes: [pid_t]) -> Bool {
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
}
