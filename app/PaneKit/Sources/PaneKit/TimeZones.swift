import Foundation

// The zones a profile's fingerprint can claim, as the sheet's menu lists them
// (ADR 0038): by region, each with its offset today. Antarctica's, the Arctic's,
// and the bare "GMT" nobody would claim are left out; UTC stands on its own.
public enum TimeZones {
	public struct Choice: Hashable, Identifiable, Sendable {
		public let identifier: String
		// "New York (GMT\u{2212}5)"
		public let title: String

		public var id: String { identifier }
	}

	public struct Region: Hashable, Identifiable, Sendable {
		public let name: String
		public let choices: [Choice]

		public var id: String { name }
	}

	public static let utc = "UTC"

	private static let regionNames = ["Africa", "America", "Asia", "Atlantic", "Australia", "Europe", "Indian", "Pacific"]

	public static func regions(identifiers: [String] = TimeZone.knownTimeZoneIdentifiers, now: Date = Date()) -> [Region] {
		regionNames.compactMap { name in
			let choices = identifiers
				.filter { $0.hasPrefix("\(name)/") }
				.map { Choice(identifier: $0, title: "\(place($0)) (\(offset($0, now: now)))") }
				.sorted { $0.title < $1.title }

			return choices.isEmpty ? nil : Region(name: name, choices: choices)
		}
	}

	// "America/Sao_Paulo": Sao Paulo. What the trigger and the note show.
	public static func city(_ identifier: String) -> String {
		(identifier.split(separator: "/").last.map(String.init) ?? identifier).replacingOccurrences(of: "_", with: " ")
	}

	// "America/Argentina/Buenos_Aires": Argentina, Buenos Aires. Within its region,
	// what tells it from another of the same name.
	private static func place(_ identifier: String) -> String {
		identifier.split(separator: "/").dropFirst().joined(separator: ", ").replacingOccurrences(of: "_", with: " ")
	}

	// GMT\u{2212}5, GMT+5:30, GMT+0.
	private static func offset(_ identifier: String, now: Date) -> String {
		let seconds = TimeZone(identifier: identifier)?.secondsFromGMT(for: now) ?? 0
		let minutes = abs(seconds) / 60
		let sign = seconds < 0 ? "\u{2212}" : "+"
		let hours = "GMT\(sign)\(minutes / 60)"

		return minutes % 60 == 0 ? hours : "\(hours):\(String(format: "%02d", minutes % 60))"
	}
}
