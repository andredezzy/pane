// Compares plain major.minor.patch versions. A leading "v" and any prerelease
// or build suffix are ignored: release tags for this repo are like "v0.1.1".

public struct InvalidVersion: Error, Equatable {
	public let version: String
}

public func isNewerVersion(_ candidate: String, than current: String) throws -> Bool {
	let candidateParts = try versionParts(candidate)
	let currentParts = try versionParts(current)

	for index in 0 ..< max(candidateParts.count, currentParts.count) {
		let candidatePart = index < candidateParts.count ? candidateParts[index] : 0
		let currentPart = index < currentParts.count ? currentParts[index] : 0

		if candidatePart != currentPart {
			return candidatePart > currentPart
		}
	}

	return false
}

private func versionParts(_ version: String) throws -> [Int] {
	var numeric = Substring(version)

	if numeric.first == "v" || numeric.first == "V" {
		numeric = numeric.dropFirst()
	}

	let core = numeric.split(whereSeparator: { $0 == "-" || $0 == "+" }).first ?? ""

	return try core.split(separator: ".", omittingEmptySubsequences: false).map { part in
		guard let value = Int(part) else {
			throw InvalidVersion(version: version)
		}

		return value
	}
}
