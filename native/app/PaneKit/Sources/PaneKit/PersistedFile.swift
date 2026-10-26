import Foundation

// One JSON file under the data root, in the envelope the Electron app's store
// wrote ({"state": …, "version": 0}), so its files read back unchanged.
public struct PersistedFile<State: Codable & Sendable>: Sendable {
	private struct Envelope: Codable {
		var state: State
		var version: Int
	}

	public let url: URL

	public init(directory: URL, name: String) {
		url = directory.appendingPathComponent("\(name).json")
	}

	// nil when the file does not exist yet.
	public func load() throws -> State? {
		guard FileManager.default.fileExists(atPath: url.path) else {
			return nil
		}

		return try JSONDecoder().decode(Envelope.self, from: Data(contentsOf: url)).state
	}

	public func save(_ state: State) throws {
		let encoder = JSONEncoder()
		encoder.outputFormatting = [.sortedKeys]

		try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
		try encoder.encode(Envelope(state: state, version: 0)).write(to: url, options: .atomic)
	}
}

public struct ProfilesState: Codable, Hashable, Sendable {
	public var profiles: [BrowserProfile]

	public init(profiles: [BrowserProfile] = []) {
		self.profiles = profiles
	}
}

public struct SettingsState: Codable, Hashable, Sendable {
	public var settings: AppSettings

	public init(settings: AppSettings = .defaults) {
		self.settings = settings
	}

	public init(from decoder: Decoder) throws {
		let container = try decoder.container(keyedBy: CodingKeys.self)
		settings = try container.decodeIfPresent(AppSettings.self, forKey: .settings) ?? .defaults
	}
}
