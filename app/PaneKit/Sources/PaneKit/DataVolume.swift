import DiskArbitration
import Foundation

// The encrypted volume that holds every profile's engine folder and caches
// (ADR 0113): a sparse bundle in the data root, a terabyte at most and
// sparse, whose APFS volume is encrypted with the passphrase the data key
// derives, as FileVault encrypts one: in the kernel, its files' contents and
// names alike. The image attaches before the key opens, its volume locked;
// once the key opens, the volume unlocks and mounts at Engine/Volume, out of
// the Finder. Engine/<profile id>, and the profile's caches' folder when the
// caches live apart, are links into it. The mount point is inside the
// engine's user data dir because the network service's sandbox allows only
// that and the caches' folder, by their resolved paths
// (GetNetworkContextsParentDirectory): a volume beside it would leave the
// cookies and the HTTP cache unopened. The mount point is read-only, so
// while the volume is not mounted the links lead nowhere and nothing written
// through them lands in the clear.
public struct DataVolume: Sendable {
	// A disk image tool that failed, with what it said.
	public struct Failure: Error, CustomStringConvertible {
		public let command: String
		public let status: Int32
		public let output: String

		public var description: String {
			"\(command) exited with \(status): \(output)"
		}
	}

	// A volume in the image that is not encrypted, refused (open).
	public struct NotEncrypted: Error, CustomStringConvertible {
		public let volume: String

		public var description: String {
			"the volume \(volume) is not encrypted"
		}
	}

	// A profile's folder from before the floor (ADR 0122), refused and left as
	// it is (prepare).
	public struct BeforeTheFloor: Error, CustomStringConvertible {
		public let folder: String

		public var description: String {
			"\(folder) is from before the floor: Pane no longer converts it"
		}
	}

	// An attached image's devices: its own (/dev/diskN), which detaches it
	// whole, and its encrypted volume's.
	public struct Devices: Sendable, Hashable {
		public let image: String
		public let volume: String
	}

	public let image: URL
	public let mountPoint: URL
	private let dataDirectory: URL
	private let cacheDirectory: URL?

	public init(dataDirectory: URL, cacheDirectory: URL?) {
		self.dataDirectory = dataDirectory
		self.cacheDirectory = cacheDirectory
		image = dataDirectory.appending(path: "Data.sparsebundle", directoryHint: .isDirectory)
		mountPoint = dataDirectory.appending(path: "Engine/Volume", directoryHint: .isDirectory)
	}

	public var exists: Bool {
		FileManager.default.fileExists(atPath: image.path)
	}

	public var isMounted: Bool {
		guard let resolved = realpath(mountPoint.path, nil) else {
			return false
		}

		defer { free(resolved) }
		var info = statfs()

		guard statfs(resolved, &info) == 0 else {
			return false
		}

		let mountedOn = withUnsafeBytes(of: info.f_mntonname) { String(decoding: $0.prefix { $0 != 0 }, as: UTF8.self) }
		return mountedOn == String(cString: resolved)
	}

	// MARK: Attaching

	// Attaches the image, its volume locked, or makes it the first time, or
	// finishes one a quit cut short while making it, which has no volume and
	// so holds nothing. Made, it has no file system, an APFS container on it,
	// and in that one volume, born encrypted with the passphrase, which
	// diskutil reads from stdin: no plain volume is ever made to be mounted
	// in its place. 1.2 s, where hdiutil's image with a plain volume, removed
	// once the encrypted one was added beside it, took 3.5.
	public func attachOrMake(passphrase: String) throws -> Devices {
		try prepareMountPoint()
		let made = !exists

		if made {
			try makeImage()
		}

		let attached: Attachment

		do {
			attached = try attachOnce()
		} catch {
			detachLeftovers()
			attached = try attachOnce()
		}

		if let volume = attached.volume {
			return Devices(image: attached.image, volume: volume)
		}

		let container = try attached.container ?? makeContainer(on: attached.image, made: made)
		let output = try run("/usr/sbin/diskutil", ["apfs", "addVolume", container, "APFS", "Pane", "-stdinpassphrase", "-nomount"], input: passphrase)

		// diskutil names the volume it made on a line of its own.
		let prefix = "Disk from APFS operation: "
		let line = String(decoding: output, as: UTF8.self).split(separator: "\n").last { $0.hasPrefix(prefix) }
		let volume = try line.map { $0.dropFirst(prefix.count).trimmingCharacters(in: .whitespaces) } ?? self.volume(named: "Pane", in: container)

		return Devices(image: attached.image, volume: Self.device(volume))
	}

	// A terabyte at most, sparse, with no file system. `diskutil image
	// create` where macOS has it, in 70 ms; hdiutil's takes a second.
	private func makeImage() throws {
		do {
			guard ProcessInfo.processInfo.operatingSystemVersion.majorVersion >= 26 else {
				throw CocoaError(.featureUnsupported)
			}

			try run("/usr/sbin/diskutil", ["image", "create", "blank", "--fs", "None", "--format", "UDSB", "--size", "1T", image.path])
		} catch {
			try run("/usr/bin/hdiutil", ["create", "-size", "1t", "-type", "SPARSEBUNDLE", "-layout", "NONE", image.deletingPathExtension().path, "-quiet"])
		}
	}

	// The image's APFS container, made on it unless one is there: formatting
	// erases, so only an image just made, or one diskutil finds no container
	// on, is formatted.
	private func makeContainer(on image: String, made: Bool) throws -> String {
		if !made, let container = try? container(of: image) {
			return container
		}

		try run("/sbin/newfs_apfs", ["-C", "-w", image])
		return try container(of: image)
	}

	// The image attached once, its volume locked. One with no volume yet,
	// whose making a quit cut short, stays attached for attachOrMake to
	// finish.
	public func attachImageOnce() throws -> Devices {
		let attached = try attachOnce()

		guard let volume = attached.volume else {
			throw Failure(command: "attach", status: 0, output: "no APFS volume in the image")
		}

		return Devices(image: attached.image, volume: volume)
	}

	// An attached image's devices: its own, and its APFS container's and
	// volume's, once made.
	private struct Attachment {
		let image: String
		let container: String?
		let volume: String?
	}

	// One attach, nothing cleared on failure: before the engine's singleton,
	// a Pane already running may hold the image, and keeps it. `diskutil image
	// attach` where macOS has it, since hdiutil's attach is deprecated there;
	// hdiutil on the macOS versions before it.
	private func attachOnce() throws -> Attachment {
		var output: Data

		do {
			guard ProcessInfo.processInfo.operatingSystemVersion.majorVersion >= 26 else {
				throw CocoaError(.featureUnsupported)
			}

			output = try run("/usr/sbin/diskutil", ["image", "attach", "--noMount", "--plist", image.path])
		} catch {
			output = try run("/usr/bin/hdiutil", ["attach", image.path, "-nomount", "-plist", "-noverify", "-noautoopen"])
		}

		guard
			let plist = try PropertyListSerialization.propertyList(from: output, format: nil) as? [String: Any],
			let entities = plist["system-entities"] as? [[String: Any]],
			let image = Self.imageDevice(entities)
		else {
			throw Failure(command: "attach", status: 0, output: String(decoding: output, as: UTF8.self))
		}

		let device = { (hints: [String]) in
			(entities.first { hints.contains($0["content-hint"] as? String ?? "") }?["dev-entry"] as? String).map(Self.device)
		}

		return Attachment(
			image: image,
			container: device(Self.containerHints),
			volume: device(["Apple_APFS_Volume", "41504653-0000-11AA-AA11-00306543ECAC"])
		)
	}

	// diskutil names an APFS container and volume by their role, hdiutil by
	// their type's GUID.
	private static let containerHints = ["Apple_APFS_Container", "EF57347C-0000-11AA-AA11-00306543ECAC"]

	// The image's own device among an attachment's: its whole disk, which
	// detaches it whole, not its APFS container's, a whole disk too. They
	// come in no set order: a partitioned image's EFI partition came first.
	// diskutil names them without /dev/, hdiutil with it.
	private static func imageDevice(_ entities: [[String: Any]]) -> String? {
		entities.lazy.compactMap { entity -> String? in
			guard
				let device = (entity["dev-entry"] as? String).map(Self.device),
				device.hasPrefix("/dev/disk"), device.dropFirst("/dev/disk".count).allSatisfy(\.isNumber),
				!containerHints.contains(entity["content-hint"] as? String ?? "")
			else {
				return nil
			}

			return device
		}.first
	}

	// Unlocks the attached image's volume with the passphrase and mounts it,
	// and keeps it only if it is encrypted: a plain volume put in its place
	// unlocks with nothing and would take the profiles in the clear, so its
	// image detaches before anything is written. One left unlocked mounts as
	// it is, and is checked the same. One on a passphrase from before ADR 0121
	// neither unlocks nor mounts, and nothing in it changes (ADR 0122).
	public func open(_ devices: Devices, passphrase: String) throws {
		let unlocked = Result {
			try run("/usr/sbin/diskutil", ["apfs", "unlockVolume", devices.volume, "-stdinpassphrase", "-nomount"], input: passphrase)
		}

		do {
			try mount(devices)
		} catch {
			// A volume that would not unlock says why.
			_ = try unlocked.get()
			throw error
		}

		let values = try? URL(fileURLWithPath: mountPoint.path, isDirectory: true).resourceValues(forKeys: [.volumeIsEncryptedKey])

		guard values?.volumeIsEncrypted == true else {
			try? detach(device: devices.image, force: true)
			throw NotEncrypted(volume: devices.volume)
		}
	}

	// At the mount point, out of the Finder (nobrowse), through Disk
	// Arbitration in this process: 50 ms, where diskutil takes 200 to start. One
	// a lock could not detach is still there.
	public func mount(_ devices: Devices) throws {
		guard !isMounted else {
			return
		}

		try prepareMountPoint()

		guard
			let session = DASessionCreate(kCFAllocatorDefault),
			let disk = DADiskCreateFromBSDName(kCFAllocatorDefault, session, devices.volume)
		else {
			throw Failure(command: "mount", status: 0, output: "no disk \(devices.volume)")
		}

		let queue = DispatchQueue(label: "pane.volume.mount")
		DASessionSetDispatchQueue(session, queue)
		defer { DASessionSetDispatchQueue(session, nil) }

		let result = MountResult()
		let arguments = UnsafeMutablePointer<Unmanaged<CFString>?>.allocate(capacity: 2)
		arguments[0] = Unmanaged.passRetained("nobrowse" as CFString)
		arguments[1] = nil
		defer {
			arguments[0]?.release()
			arguments.deallocate()
		}

		DADiskMountWithArguments(disk, mountPoint as CFURL, DADiskMountOptions(kDADiskMountOptionDefault), { _, dissenter, context in
			let result = Unmanaged<MountResult>.fromOpaque(context!).takeUnretainedValue()
			result.status = dissenter.map(DADissenterGetStatus) ?? 0
			result.done.signal()
		}, Unmanaged.passUnretained(result).toOpaque(), arguments)

		guard result.done.wait(timeout: .now() + 30) == .success, result.status == 0 else {
			throw Failure(command: "mount \(devices.volume)", status: result.status ?? -1, output: "")
		}

		// The volume's root, the user's alone.
		chmod(mountPoint.path, 0o700)

		// Spotlight leaves it alone: mdutil can't tell its state on a volume out
		// of the Finder, and an index would outlive the lock (ADR 0121).
		let unindexed = mountPoint.appending(path: ".metadata_never_index")

		if !FileManager.default.fileExists(atPath: unindexed.path) {
			FileManager.default.createFile(atPath: unindexed.path, contents: nil)
		}

		stopFileEventsLog()
	}

	// macOS logs the names of the files that change at a volume's root, in the
	// clear, and an earlier Pane's logs name each site's folders: an empty
	// `no_log` turns that off, and what is there goes.
	private func stopFileEventsLog() {
		let events = mountPoint.appending(path: ".fseventsd", directoryHint: .isDirectory)
		let noLog = events.appending(path: "no_log")

		guard !FileManager.default.fileExists(atPath: noLog.path) else {
			return
		}

		try? FileManager.default.createDirectory(at: events, withIntermediateDirectories: true)
		FileManager.default.createFile(atPath: noLog.path, contents: nil)

		for item in (try? FileManager.default.contentsOfDirectory(at: events, includingPropertiesForKeys: nil)) ?? []
			where !["no_log", "fseventsd-uuid"].contains(item.lastPathComponent)
		{
			try? FileManager.default.removeItem(at: item)
		}
	}

	private final class MountResult: @unchecked Sendable {
		let done = DispatchSemaphore(value: 0)
		var status: DAReturn?
	}

	private static func device(_ name: String) -> String {
		name.hasPrefix("/dev/") ? name : "/dev/\(name)"
	}

	// The APFS container a volume is in.
	private func container(of volume: String) throws -> String {
		let info = try run("/usr/sbin/diskutil", ["info", "-plist", volume])

		guard
			let plist = try PropertyListSerialization.propertyList(from: info, format: nil) as? [String: Any],
			let container = plist["APFSContainerReference"] as? String
		else {
			throw Failure(command: "diskutil info", status: 0, output: String(decoding: info, as: UTF8.self))
		}

		return container
	}

	private func volume(named name: String, in container: String) throws -> String {
		let list = try run("/usr/sbin/diskutil", ["apfs", "list", "-plist", container])

		guard
			let plist = try PropertyListSerialization.propertyList(from: list, format: nil) as? [String: Any],
			let containers = plist["Containers"] as? [[String: Any]],
			let volumes = containers.first?["Volumes"] as? [[String: Any]],
			let identifier = volumes.first(where: { $0["Name"] as? String == name })?["DeviceIdentifier"] as? String
		else {
			throw Failure(command: "diskutil apfs list", status: 0, output: String(decoding: list, as: UTF8.self))
		}

		return Self.device(identifier)
	}

	// By the image's device, which detaches it whole, or, for a crashed
	// Pane's, the mount point.
	public func detach(device: String? = nil, force: Bool = false) throws {
		try run("/usr/bin/hdiutil", ["detach", device ?? mountPoint.path, "-quiet"] + (force ? ["-force"] : []))
	}

	// Every attachment of this image hdiutil lists, by its device.
	public func detachLeftovers() {
		guard
			let output = try? run("/usr/bin/hdiutil", ["info", "-plist"]),
			let info = try? PropertyListSerialization.propertyList(from: output, format: nil) as? [String: Any],
			let images = info["images"] as? [[String: Any]]
		else {
			return
		}

		let path = Self.realPath(image.path)

		for attached in images where (attached["image-path"] as? String).map(Self.realPath) == path {
			if let device = (attached["system-entities"] as? [[String: Any]]).flatMap(Self.imageDevice) {
				try? detach(device: device, force: true)
			}
		}
	}

	// With its links resolved (/var is /private/var), as hdiutil may name it.
	private static func realPath(_ path: String) -> String {
		guard let resolved = realpath(path, nil) else {
			return path
		}

		defer { free(resolved) }
		return String(cString: resolved)
	}

	// Runs `helper` (pane-volume-watch), which detaches this image, on whichever
	// device it then has, once this process ends, a crash included.
	public func watch(helper: URL) throws {
		let process = Process()
		process.executableURL = helper
		process.arguments = [String(getpid()), image.path]
		try process.run()
	}

	// Made once, and read-only while nothing is attached on it.
	private func prepareMountPoint() throws {
		try FileManager.default.createDirectory(at: mountPoint, withIntermediateDirectories: true)

		if !isMounted {
			chmod(mountPoint.path, 0o500)
		}
	}

	// MARK: Profiles

	// A profile's folder in the volume: Profile, the engine's, and Cache.
	public func folder(profile id: String) -> URL {
		mountPoint.appending(path: id, directoryHint: .isDirectory)
	}

	// Makes the profile's folders in the attached volume and the links to
	// them, before the engine opens the profile. One that saves no history loses what it wrote
	// while it did (ADR 0119): the leveldb log of its downloads, which a deletion
	// does not rewrite, beside which is only Chrome's in-product help, which Pane
	// does not show; and its caches, which it now keeps in memory.
	//
	// A profile an earlier Pane wrote and never converted (ADR 0121) has its
	// Preferences and no marker. It is refused, untouched: its tab shows the sad
	// tab, and the engine, which stops at a plain store, never sees it. A new
	// profile has nothing to convert, and is marked before its first file.
	public func prepare(profile id: String, savesHistory: Bool = true) throws {
		let folder = folder(profile: id)
		let marker = folder.appending(path: Self.encryptedMarker)
		let marked = FileManager.default.fileExists(atPath: marker.path)

		guard marked || !FileManager.default.fileExists(atPath: folder.appending(path: "Profile/Preferences").path) else {
			throw BeforeTheFloor(folder: folder.path)
		}

		try FileManager.default.createDirectory(at: folder.appending(path: "Profile"), withIntermediateDirectories: true)
		try FileManager.default.createDirectory(at: folder.appending(path: "Cache"), withIntermediateDirectories: true)
		try link(engineFolder(id), to: folder.appending(path: "Profile"))

		if let caches = cacheFolder(id) {
			try link(caches, to: folder.appending(path: "Cache"))
		}

		if !marked {
			FileManager.default.createFile(atPath: marker.path, contents: nil)
		}

		sweepConversionLeftovers(in: folder)

		if !savesHistory {
			// The caches are in Cache, or beside the profile's own files where the
			// data root is not under the home's Application Support.
			for item in ["Profile/shared_proto_db", "Profile/Cache", "Profile/Code Cache", "Cache/Cache", "Cache/Code Cache"] {
				try? FileManager.default.removeItem(at: folder.appending(path: item))
			}
		}
	}

	// An empty file beside the profile's files, in the volume: they are all in
	// their encrypted formats (ADR 0121).
	private static let encryptedMarker = ".files-encrypted"

	// What the conversion left in a profile it finished (ADR 0121), which nothing
	// reads now: its safety copy, the opens it counted, and the repair's mark. This
	// stays until a later floor, one no install still has these under.
	private func sweepConversionLeftovers(in folder: URL) {
		for name in (try? FileManager.default.contentsOfDirectory(atPath: folder.path)) ?? []
			where name == "SafetyCopy" || name == ".files-repaired" || name.hasPrefix(".safety-")
		{
			try? FileManager.default.removeItem(at: folder.appending(path: name))
		}
	}

	// The profile's links, or a plain folder refused in their place, and its
	// folder in the attached volume.
	public func remove(profile id: String) throws {
		for url in [engineFolder(id), cacheFolder(id), folder(profile: id)].compactMap(\.self)
			where (try? FileManager.default.attributesOfItem(atPath: url.path)) != nil
		{
			try FileManager.default.removeItem(at: url)
		}
	}

	private func engineFolder(_ id: String) -> URL {
		dataDirectory.appending(path: "Engine/\(id)", directoryHint: .isDirectory)
	}

	private func cacheFolder(_ id: String) -> URL? {
		cacheDirectory?.appending(path: "Engine/\(id)", directoryHint: .isDirectory)
	}

	// A directory, not a link to one.
	private func isPlainFolder(_ url: URL) -> Bool {
		(try? FileManager.default.attributesOfItem(atPath: url.path)[.type] as? FileAttributeType) == .typeDirectory
	}

	// A plain folder in the link's place, an earlier Pane's site data never moved
	// into the volume, stays there, and the profile is refused (ADR 0122).
	private func link(_ link: URL, to target: URL) throws {
		if (try? FileManager.default.destinationOfSymbolicLink(atPath: link.path)) == target.path {
			return
		}

		if isPlainFolder(link) {
			throw BeforeTheFloor(folder: link.path)
		}

		if (try? FileManager.default.attributesOfItem(atPath: link.path)) != nil {
			try FileManager.default.removeItem(at: link)
		}

		try FileManager.default.createDirectory(at: link.deletingLastPathComponent(), withIntermediateDirectories: true)
		try FileManager.default.createSymbolicLink(at: link, withDestinationURL: target)
	}

	// A disk image tool, with `input` on its standard input (a passphrase is
	// never an argument, which other processes can read), and its output.
	@discardableResult
	private func run(_ tool: String, _ arguments: [String], input: String? = nil) throws -> Data {
		let process = Process()
		let stdin = Pipe(), stdout = Pipe(), stderr = Pipe()
		process.executableURL = URL(fileURLWithPath: tool)
		process.arguments = arguments
		process.standardInput = stdin
		process.standardOutput = stdout
		process.standardError = stderr
		try process.run()

		if let input {
			stdin.fileHandleForWriting.write(Data(input.utf8))
		}

		try stdin.fileHandleForWriting.close()
		let output = stdout.fileHandleForReading.readDataToEndOfFile()
		let errors = stderr.fileHandleForReading.readDataToEndOfFile()
		process.waitUntilExit()

		guard process.terminationStatus == 0 else {
			throw Failure(
				command: ([tool] + arguments.prefix(2)).joined(separator: " "),
				status: process.terminationStatus,
				output: String(decoding: errors + output, as: UTF8.self)
			)
		}

		return output
	}
}
