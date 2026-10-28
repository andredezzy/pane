import PaneKit
import Testing

@Suite("extension prompt text")
struct ExtensionPromptTextTests {
	@Test("adding names Pane, with the name's quotes curled")
	func addingNamesPane() {
		#expect(ExtensionPromptText.title(acceptTitle: "Add extension", extensionName: "Dark Reader", chromeTitle: "Add \"Dark Reader\"?") == "Add \u{201C}Dark Reader\u{201D} to Pane?")
		#expect(ExtensionPromptText.acceptTitle("Add extension") == "Add to Pane")
	}

	@Test("Chrome's other prompts keep their title and button, quotes curled")
	func otherPrompts() {
		let title = "\"uBlock Origin Lite\" needs new permissions"

		#expect(ExtensionPromptText.title(acceptTitle: "Accept permissions", extensionName: "uBlock Origin Lite", chromeTitle: title) == "\u{201C}uBlock Origin Lite\u{201D} needs new permissions")
		#expect(ExtensionPromptText.acceptTitle("Re-enable") == "Re-enable")
	}

	@Test("curls only the first and last straight quotes, and only a pair")
	func curlyQuotes() {
		#expect(ExtensionPromptText.curlyQuoted("Add \"A \"B\" C\"?") == "Add \u{201C}A \"B\" C\u{201D}?")
		#expect(ExtensionPromptText.curlyQuoted("Add \"A?") == "Add \"A?")
		#expect(ExtensionPromptText.curlyQuoted("No quotes") == "No quotes")
	}

	@Test("removing asks with the name, and reports abuse on the Web Store")
	func removing() {
		#expect(ExtensionPromptText.removeTitle("Dark Reader") == "Remove \u{201C}Dark Reader\u{201D}?")
		#expect(ExtensionPromptText.reportAbuseURL("abc")?.absoluteString == "https://chrome.google.com/webstore/report/abc?utm_source=chrome-remove-extension-dialog")
	}
}
