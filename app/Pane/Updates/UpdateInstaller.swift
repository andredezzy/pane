import Foundation
import PaneKit

// Pane installing its own update (ADR 0092). Once the dmg checks out, the Pane
// inside it is staged beside the running app, hidden; at quit the two bundles
// swap in one rename, and a detached shell, once Pane has exited, removes the
// old bundle and the dmg and, for Restart to update, opens the new Pane. A
// step that fails leaves the running app as it was, and the user installs
// from the dmg.
struct UpdateInstaller: Sendable {
	struct Failure: Error, CustomStringConvertible {
		let description: String
	}

	// The running app's bundle, which the update replaces.
	let bundle: URL
	private let identifier: String

	// Nil when Pane cannot replace itself: translocated, on a disk image, or in
	// a folder this user cannot write.
	init?(bundle: Bundle = .main) {
		guard let path = UpdateInstall.target(bundlePath: bundle.bundlePath), let identifier = bundle.bundleIdentifier,
		      FileManager.default.isWritableFile(atPath: (path as NSString).deletingLastPathComponent) else {
			return nil
		}

		self.bundle = URL(fileURLWithPath: path)
		self.identifier = identifier
	}

	// Mounts the dmg, checks the Pane in it is this app at `version` with its
	// signature whole, and copies it beside the running one. Off the main
	// thread: it reads the whole app.
	func stage(_ dmg: URL, version: String) throws -> URL {
		let fileManager = FileManager.default
		let mount = fileManager.temporaryDirectory.appending(path: "pane-update-\(UUID().uuidString)")
		try fileManager.createDirectory(at: mount, withIntermediateDirectories: true)

		// The dmg matched the release's SHA-256, so its own checksums need no
		// second read.
		try Self.run("/usr/bin/hdiutil", ["attach", dmg.path, "-nobrowse", "-readonly", "-noautoopen", "-noverify", "-mountpoint", mount.path])

		defer {
			if (try? Self.run("/usr/bin/hdiutil", ["detach", mount.path, "-quiet"])) == nil {
				try? Self.run("/usr/bin/hdiutil", ["detach", mount.path, "-force", "-quiet"])
			}

			try? fileManager.removeItem(at: mount)
		}

		let app = mount.appending(path: "Pane.app")

		guard let info = NSDictionary(contentsOf: app.appending(path: "Contents/Info.plist")),
		      info["CFBundleIdentifier"] as? String == identifier, info["PaneVersion"] as? String == version else {
			throw Failure(description: "the dmg holds no Pane \(version)")
		}

		try Self.run("/usr/bin/codesign", ["--verify", "--deep", "--strict", app.path])

		let staged = bundle.deletingLastPathComponent().appending(path: UpdateInstall.stagedName(version: version))
		try? fileManager.removeItem(at: staged)
		try Self.run("/usr/bin/ditto", [app.path, staged.path])
		// The dmg left quarantine (ADR 0090); a copy must not carry it either.
		try? Self.run("/usr/bin/xattr", ["-dr", "com.apple.quarantine", staged.path])

		return staged
	}

	// At quit: swaps the staged bundle in for the running one in one rename,
	// so the app's path never stands empty. This process keeps the files it has
	// open; the shell waits for it to end before it removes the old bundle.
	func install(_ staged: URL, dmg: URL?, relaunch: Bool) throws {
		guard renamex_np(bundle.path, staged.path, UInt32(RENAME_SWAP)) == 0 else {
			throw Failure(description: "the swap failed: \(String(cString: strerror(errno)))")
		}

		// $1 Pane's process, $2 the old bundle (where the staged one was), $3
		// the new one, $4 the dmg, $5 whether to open the new Pane, then what
		// `open` passes on: Pane's own PANE_ variables, which only scenarios
		// set, so the new Pane opens on the same throwaway data.
		let script = #"pid=$1 old=$2 app=$3 dmg=$4 relaunch=$5; shift 5; while kill -0 "$pid" 2>/dev/null; do sleep 0.05; done; rm -rf "$old"; [ -n "$dmg" ] && rm -f "$dmg"; [ "$relaunch" = 1 ] && /usr/bin/open "$@" "$app"; exit 0"#
		let passed = ProcessInfo.processInfo.environment.filter { $0.key.hasPrefix("PANE_") }.sorted { $0.key < $1.key }.flatMap { ["--env", "\($0.key)=\($0.value)"] }
		let process = Process()
		process.executableURL = URL(fileURLWithPath: "/bin/sh")
		process.arguments = ["-c", script, "pane-update", String(ProcessInfo.processInfo.processIdentifier), staged.path, bundle.path, dmg?.path ?? "", relaunch ? "1" : "0"] + passed
		try process.run()
	}

	// What an earlier install left: a copy staged and never swapped in, or an
	// old bundle the shell did not get to remove.
	func removeLeftovers() {
		let folder = bundle.deletingLastPathComponent()

		for name in (try? FileManager.default.contentsOfDirectory(atPath: folder.path)) ?? [] where UpdateInstall.isLeftover(name) {
			try? FileManager.default.removeItem(at: folder.appending(path: name))
		}
	}

	private static func run(_ tool: String, _ arguments: [String]) throws {
		let process = Process()
		process.executableURL = URL(fileURLWithPath: tool)
		process.arguments = arguments
		let errors = Pipe()
		process.standardOutput = FileHandle.nullDevice
		process.standardError = errors
		try process.run()
		let message = String(decoding: errors.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
		process.waitUntilExit()

		guard process.terminationStatus == 0 else {
			throw Failure(description: "\((tool as NSString).lastPathComponent) exited \(process.terminationStatus): \(message.trimmingCharacters(in: .whitespacesAndNewlines))")
		}
	}
}
