import Foundation

// pane-volume-watch <pid> <image>: waits for Pane's process to end, a crash
// included, then detaches the encrypted volume (ADR 0113), so the data never
// stays open without Pane. kqueue's NOTE_EXIT wakes it once, at the end: nothing
// polls. One serves a Pane for its whole life, through every lock: a lock
// detaches the image and the unlock attaches it again, on whichever device is
// free, so it is found by its path when the time comes.
let arguments = CommandLine.arguments

guard arguments.count == 3, let pid = pid_t(arguments[1]) else {
	exit(64)
}

// A session of its own: a kill of Pane's process group, which ended Pane and
// this watchdog together and left the volume mounted, or the hangup of the
// terminal Pane was started from, does not reach it.
signal(SIGHUP, SIG_IGN)
setsid()

// The path with its links resolved (/var is /private/var), as hdiutil reports it.
func real(_ path: String) -> String {
	guard let resolved = realpath(path, nil) else {
		return path
	}

	defer { free(resolved) }
	return String(cString: resolved)
}

let image = real(arguments[2])
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

// The image's own device, a whole disk, which detaches it whole, and not its
// APFS container's, a whole disk too, which hdiutil names by its type's GUID.
func imageDevice(_ entities: [[String: Any]]) -> String? {
	entities.lazy.compactMap { entity -> String? in
		guard
			let device = entity["dev-entry"] as? String,
			device.hasPrefix("/dev/disk"), device.dropFirst("/dev/disk".count).allSatisfy(\.isNumber),
			entity["content-hint"] as? String != "EF57347C-0000-11AA-AA11-00306543ECAC"
		else {
			return nil
		}

		return device
	}.first
}

// The data's lock (PaneKit's DataLock), which Pane let go of as it ended: another
// Pane that has taken it since holds the image now, and it stays. Held from here
// to the end, so one that comes next waits for the detach.
let dataLock = open(URL(fileURLWithPath: arguments[2]).deletingLastPathComponent().appendingPathComponent("Pane.lock").path, O_RDWR | O_CREAT | O_CLOEXEC, 0o600)

if dataLock >= 0, flock(dataLock, LOCK_EX | LOCK_NB) != 0 {
	exit(0)
}

let info = (try? PropertyListSerialization.propertyList(from: hdiutil(["info", "-plist"]), format: nil)) as? [String: Any]

for attached in info?["images"] as? [[String: Any]] ?? [] where (attached["image-path"] as? String).map(real) == image {
	if let device = imageDevice(attached["system-entities"] as? [[String: Any]] ?? []) {
		_ = hdiutil(["detach", device, "-force", "-quiet"])
	}
}
