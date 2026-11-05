import Foundation
import PaneKit
import Testing

@Suite("TimeZones")
struct TimeZonesTests {
	// 15 January 2027: New York on standard time.
	private let january = Date(timeIntervalSince1970: 1_800_000_000)

	@Test("the regions, in order, leave out what nobody would claim")
	func regions() {
		let names = TimeZones.regions(identifiers: ["Antarctica/Troll", "Arctic/Longyearbyen", "GMT", "Pacific/Fiji", "Africa/Lagos"]).map(\.name)

		#expect(names == ["Africa", "Pacific"])
	}

	@Test("each choice names its place and its offset, sorted within its region")
	func choices() throws {
		let identifiers = ["America/New_York", "America/Argentina/Buenos_Aires", "Asia/Kolkata", "Asia/Tokyo"]
		let regions = TimeZones.regions(identifiers: identifiers, now: january)
		let america = try #require(regions.first { $0.name == "America" })
		let asia = try #require(regions.first { $0.name == "Asia" })

		#expect(america.choices.map(\.title) == ["Argentina, Buenos Aires (GMT\u{2212}3)", "New York (GMT\u{2212}5)"])
		#expect(america.choices.last?.identifier == "America/New_York")
		#expect(asia.choices.map(\.title) == ["Kolkata (GMT+5:30)", "Tokyo (GMT+9)"])
	}

	@Test("a zone's city, for the trigger and the note")
	func city() {
		#expect(TimeZones.city("America/Sao_Paulo") == "Sao Paulo")
		#expect(TimeZones.city("America/Argentina/Buenos_Aires") == "Buenos Aires")
		#expect(TimeZones.city(TimeZones.utc) == "UTC")
	}

	@Test("every region lists what this Mac knows")
	func known() {
		#expect(TimeZones.regions().map(\.name) == ["Africa", "America", "Asia", "Atlantic", "Australia", "Europe", "Indian", "Pacific"])
		#expect(TimeZones.regions().flatMap(\.choices).contains { $0.identifier == "America/New_York" })
	}
}
