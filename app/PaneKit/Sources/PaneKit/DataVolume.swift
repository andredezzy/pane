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

	// What the mounted volume holds, in bytes, for compact.
	public var usedBytes: Int64? {
		var info = statfs()

		guard isMounted, statfs(mountPoint.path, &info) == 0 else {
			return nil
		}

		return Int64(info.f_blocks - info.f_bfree) * Int64(info.f_bsize)
	}

	// MARK: Attaching

	// Made once, and left attached with its volume unlocked: hdiutil makes
	// the image with a plain APFS volume, which goes once a volume born
	// encrypted with the passphrase is added beside it.
	public func create(passphrase: String) throws -> Devices {
		try prepareMountPoint()
		try run("/usr/bin/hdiutil", [
			"create", "-size", "1t", "-type", "SPARSEBUNDLE", "-fs", "APFS", "-volname", "Plain",
			image.deletingPathExtension().path, "-quiet",
		])

		let plain = try attachImage()
		let container = try self.container(of: plain.volume)
		try run("/usr/sbin/diskutil", ["apfs", "addVolume", container, "APFS", "Pane", "-stdinpassphrase", "-nomount"], input: passphrase)
		try run("/usr/sbin/diskutil", ["apfs", "deleteVolume", plain.volume])

		return Devices(image: plain.image, volume: try volume(named: "Pane", in: container))
	}

	// Attaches the image without mounting it: its volume stays locked. One a
	// crash left attached goes first, when the attach finds it.
	public func attachImage() throws -> Devices {
		do {
			return try attachImageOnce()
		} catch {
			detachLeftovers()
			return try attachImageOnce()
		}
	}

	// One attach, nothing cleared on failure: before the engine's singleton,
	// a Pane already running may hold the image, and keeps it. `diskutil image
	// attach` where macOS has it, since hdiutil's attach is deprecated there;
	// hdiutil on the macOS versions before it.
	public func attachImageOnce() throws -> Devices {
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
			let image = entities.first?["dev-entry"] as? String
		else {
			throw Failure(command: "attach", status: 0, output: String(decoding: output, as: UTF8.self))
		}

		// diskutil names an APFS volume by its role, hdiutil by its type's
		// GUID.
		let volume = entities.first {
			["Apple_APFS_Volume", "41504653-0000-11AA-AA11-00306543ECAC"].contains($0["content-hint"] as? String)
		}?["dev-entry"] as? String

		guard let volume else {
			throw Failure(command: "attach", status: 0, output: "no APFS volume in \(entities)")
		}

		return Devices(image: Self.device(image), volume: Self.device(volume))
	}

	// Unlocks the attached image's volume with the passphrase, then mounts
	// it. A volume just made is unlocked already, and mounts as it is.
	public func open(_ devices: Devices, passphrase: String) throws {
		do {
			try run("/usr/sbin/diskutil", ["apfs", "unlockVolume", devices.volume, "-stdinpassphrase", "-nomount"], input: passphrase)
		} catch {
			do {
				try mount(devices)
				return
			} catch {}

			throw error
		}

		try mount(devices)
	}

	// At the mount point, out of the Finder (nobrowse), through Disk
	// Arbitration in this process: 50 ms, where diskutil takes 200 to start.
	public func mount(_ devices: Devices) throws {
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
			if let device = (attached["system-entities"] as? [[String: Any]])?.first?["dev-entry"] as? String {
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

	// Runs `helper` (pane-volume-watch), which detaches the image's `device`
	// once this process ends, a crash included, if it still holds this image.
	public func watch(device: String, helper: URL) throws {
		let process = Process()
		process.executableURL = helper
		process.arguments = [String(getpid()), device, image.path]
		try process.run()
	}

	// The space the sparse bundle keeps after deletions goes back to the
	// disk once it holds over 1 GB more than the volume used: on the detached
	// image, which is not encrypted itself.
	public func compact(used: Int64) throws {
		guard size(image) - used > 1 << 30 else {
			return
		}

		try run("/usr/bin/hdiutil", ["compact", image.path, "-quiet"])
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
	// them, before the engine opens the profile. A plain folder still in
	// their place moves in first.
	public func prepare(profile id: String) throws {
		if isPlainFolder(engineFolder(id)) {
			try moveIn(profiles: [id]) { _ in }
		}

		let folder = folder(profile: id)
		try FileManager.default.createDirectory(at: folder.appending(path: "Profile"), withIntermediateDirectories: true)
		try FileManager.default.createDirectory(at: folder.appending(path: "Cache"), withIntermediateDirectories: true)
		try link(engineFolder(id), to: folder.appending(path: "Profile"))

		if let caches = cacheFolder(id) {
			if isPlainFolder(caches) {
				try stage(caches, in: cacheDirectory, id: UUID().uuidString)
			}

			try link(caches, to: folder.appending(path: "Cache"))
		}
	}

	// Moves the plain profile folders an earlier Pane left (Engine/<id>) into
	// the attached volume, each behind its link, and stages them, and their
	// caches, which rebuild, for the panic wipe's erase (PanicWipe.eraseStaged).
	// `progress` gets the share copied, 0 to 1. Each folder is staged only
	// once its copy is whole, so one cut short is copied again.
	public func moveIn(profiles ids: [String], progress: (Double) -> Void) throws {
		let plain = ids.filter { isPlainFolder(engineFolder($0)) }
		let total = plain.reduce(Int64(0)) { $0 + size(engineFolder($1)) }
		let move = UUID().uuidString
		var copied: Int64 = 0

		for id in plain {
			let target = folder(profile: id).appending(path: "Profile", directoryHint: .isDirectory)

			if FileManager.default.fileExists(atPath: target.path) {
				try FileManager.default.removeItem(at: target)
			}

			try FileManager.default.createDirectory(at: folder(profile: id), withIntermediateDirectories: true)
			try copy(engineFolder(id), to: target) { size in
				copied += size
				progress(total > 0 ? min(Double(copied) / Double(total), 1) : 1)
			}

			try stage(engineFolder(id), in: dataDirectory, id: move)
			try link(engineFolder(id), to: target)

			if let caches = cacheFolder(id), isPlainFolder(caches) {
				try stage(caches, in: cacheDirectory, id: move)
			}
		}

		progress(1)
	}

	// Whether an earlier Pane's plain folder is still in the profile's place.
	public func hasPlainFolder(profile id: String) -> Bool {
		isPlainFolder(engineFolder(id))
	}

	// Whether Engine holds any profile's plain folder: a profile's is named by
	// its id, a UUID, where the engine's own are not (Default, and the rest).
	public var hasPlainProfiles: Bool {
		let entries = (try? FileManager.default.contentsOfDirectory(atPath: dataDirectory.appending(path: "Engine").path)) ?? []
		return entries.contains { UUID(uuidString: $0) != nil && hasPlainFolder(profile: $0) }
	}

	// The profile's links, its plain folders if an earlier Pane left any, and
	// its folder in the attached volume.
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

	private func link(_ link: URL, to target: URL) throws {
		if (try? FileManager.default.destinationOfSymbolicLink(atPath: link.path)) == target.path {
			return
		}

		if (try? FileManager.default.attributesOfItem(atPath: link.path)) != nil {
			try FileManager.default.removeItem(at: link)
		}

		try FileManager.default.createDirectory(at: link.deletingLastPathComponent(), withIntermediateDirectories: true)
		try FileManager.default.createSymbolicLink(at: link, withDestinationURL: target)
	}

	// Into `root`'s Wiping folder, where PanicWipe erases it.
	private func stage(_ url: URL, in root: URL?, id: String) throws {
		guard let root else {
			return
		}

		let folder = root.appending(path: "\(PanicWipe.stagingName)/\(id)", directoryHint: .isDirectory)
		try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
		try FileManager.default.moveItem(at: url, to: folder.appending(path: url.lastPathComponent))
	}

	// File by file, so `copied` hears of each one's size. Links are copied
	// as links. The enumerator gives each path relative to `source`, as the
	// link /var → /private/var would otherwise shift a path made from URLs.
	private func copy(_ source: URL, to target: URL, copied: (Int64) -> Void) throws {
		let fileManager = FileManager.default
		try fileManager.createDirectory(at: target, withIntermediateDirectories: true)

		guard let entries = fileManager.enumerator(atPath: source.path) else {
			return
		}

		while let path = entries.nextObject() as? String {
			let from = source.appending(path: path)
			let to = target.appending(path: path)

			switch entries.fileAttributes?[.type] as? FileAttributeType {
			case .typeSymbolicLink?:
				try fileManager.createSymbolicLink(atPath: to.path, withDestinationPath: fileManager.destinationOfSymbolicLink(atPath: from.path))
			case .typeDirectory?:
				try fileManager.createDirectory(at: to, withIntermediateDirectories: true)
			default:
				try fileManager.copyItem(at: from, to: to)
				copied((entries.fileAttributes?[.size] as? NSNumber)?.int64Value ?? 0)
			}
		}
	}

	// The files' sizes under `folder`.
	private func size(_ folder: URL) -> Int64 {
		guard let entries = FileManager.default.enumerator(atPath: folder.path) else {
			return 0
		}

		var total: Int64 = 0

		while entries.nextObject() != nil {
			if entries.fileAttributes?[.type] as? FileAttributeType == .typeRegular {
				total += (entries.fileAttributes?[.size] as? NSNumber)?.int64Value ?? 0
			}
		}

		return total
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
