import Foundation

// pane-volume-watch <pid> <device> <image>: waits for Pane's process to end, a
// crash included, then detaches the encrypted volume (ADR 0113), so the data
// never stays open without Pane. kqueue's NOTE_EXIT wakes it once, at the end:
// nothing polls. It detaches the device only while it still holds `image`,
// since a device's number goes to the next image attached after it.
let arguments = CommandLine.arguments

guard arguments.count == 4, let pid = pid_t(arguments[1]), arguments[2].hasPrefix("/dev/disk") else {
	exit(64)
}

// The path with its links resolved (/var is /private/var), as hdiutil reports it.
func real(_ path: String) -> String {
	guard let resolved = realpath(path, nil) else {
		return path
	}

	defer { free(resolved) }
	return String(cString: resolved)
}

let device = arguments[2]
let image = real(arguments[3])
let queue = kqueue()
var change = kevent(ident: UInt(pid), filter: Int16(EVFILT_PROC), flags: UInt16(EV_ADD | EV_ONESHOT), fflags: NOTE_EXIT, data: 0, udata: nil)

// A process already gone fails to register (ESRCH): detach at once.
if kevent(queue, &change, 1, nil, 0, nil) == 0 {
	var event = kevent()

	while kevent(queue, nil, 0, &event, 1, nil) == -1, errno == EINTR {}
}

func hdiutil(_ arguments: [String]) -> Data {
	let process = Process()
	let output = Pipe()
	process.executableURL = URL(fileURLWithPath: "/usr/bin/hdiutil")
	process.arguments = arguments
	process.standardOutput = output
	process.standardError = FileHandle.nullDevice

	guard (try? process.run()) != nil else {
		return Data()
	}

	let data = output.fileHandleForReading.readDataToEndOfFile()
	process.waitUntilExit()
	return data
}

let info = (try? PropertyListSerialization.propertyList(from: hdiutil(["info", "-plist"]), format: nil)) as? [String: Any]
let images = info?["images"] as? [[String: Any]] ?? []
let holdsImage = images.contains { attached in
	(attached["image-path"] as? String).map(real) == image
		&& (attached["system-entities"] as? [[String: Any]] ?? []).contains { ($0["dev-entry"] as? String) == device }
}

if holdsImage {
	_ = hdiutil(["detach", device, "-force", "-quiet"])
}
