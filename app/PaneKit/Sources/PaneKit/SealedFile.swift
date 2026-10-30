import CryptoKit
import Foundation

// One file under the data root, sealed under the data key's files key (ADR
// 0113): AES-GCM over the state's JSON, bound to the file's name, so a box
// moved to another file does not open. A plain file never reads as one: only
// the move into the volume reads the plain files these replace, through
// PersistedFile.
public struct SealedFile<State: Codable & Sendable>: Sendable {
	private struct Disk: Codable {
		// AES-GCM's combined box (nonce, ciphertext, and tag), in base64.
		var box: String
	}

	public let url: URL
	private let name: String

	public init(directory: URL, name: String) {
		self.name = name
		url = directory.appendingPathComponent("\(name).json")
	}

	// nil when the file does not exist yet. Throws for another key, and a box
	// changed or moved since it was sealed.
	public func load(key: SymmetricKey) throws -> State? {
		guard FileManager.default.fileExists(atPath: url.path) else {
			return nil
		}

		let disk = try JSONDecoder().decode(Disk.self, from: Data(contentsOf: url))

		guard let combined = Data(base64Encoded: disk.box) else {
			throw CryptoKitError.incorrectParameterSize
		}

		let data = try AES.GCM.open(AES.GCM.SealedBox(combined: combined), using: key, authenticating: Data(name.utf8))
		return try JSONDecoder().decode(State.self, from: data)
	}

	public func save(_ state: State, key: SymmetricKey) throws {
		let box = try AES.GCM.seal(JSONEncoder().encode(state), using: key, authenticating: Data(name.utf8))

		guard let combined = box.combined else {
			throw CryptoKitError.incorrectParameterSize
		}

		try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
		try JSONEncoder().encode(Disk(box: combined.base64EncodedString())).write(to: url, options: .atomic)
	}
}
