import Foundation
import PaneKit
import Testing

@Suite("AddressInput")
struct AddressInputTests {
	@Test("keeps web URLs as they are")
	func webURL() {
		#expect(AddressInput.url(for: "https://example.com/a?b=c")?.absoluteString == "https://example.com/a?b=c")
		#expect(AddressInput.url(for: "http://example.com")?.absoluteString == "http://example.com")
	}

	@Test("loads bare hosts over https")
	func bareHost() {
		#expect(AddressInput.url(for: "example.com")?.absoluteString == "https://example.com")
		#expect(AddressInput.url(for: "localhost:3000")?.absoluteString == "https://localhost:3000")
	}

	@Test("searches anything else")
	func search() {
		#expect(AddressInput.url(for: "pane browser")?.absoluteString == "https://www.google.com/search?q=pane%20browser")
		#expect(AddressInput.url(for: "a.b c")?.absoluteString == "https://www.google.com/search?q=a.b%20c")
	}

	@Test("ignores blank input")
	func blank() {
		#expect(AddressInput.url(for: "   ") == nil)
	}
}
