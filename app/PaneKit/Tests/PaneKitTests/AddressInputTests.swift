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

	@Test("a web address at rest is its domain and title; other text has no label")
	func label() {
		let label = AddressInput.label(for: "https://www.example.com/a", title: " A page ")
		#expect(label?.domain == "example.com")
		#expect(label?.title == "A page")
		#expect(AddressInput.label(for: "https://example.com/a", title: "https://example.com/a")?.title == "")
		#expect(AddressInput.label(for: "data:text/html,a", title: "a") == nil)
		#expect(AddressInput.label(for: "pane browser", title: "") == nil)
	}
}
