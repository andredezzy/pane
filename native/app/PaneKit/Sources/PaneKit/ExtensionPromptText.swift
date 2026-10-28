import Foundation

// What the popups that add or remove an extension say. Adding names Pane, as
// Dia's dialog names Dia; Chrome's other prompts (new permissions,
// re-enabling) keep their titles, their quotes curled as Remove's are.
public enum ExtensionPromptText {
	// Chrome's accept button when it adds an extension.
	static let chromeAdd = "Add extension"

	public static func title(acceptTitle: String, extensionName: String, chromeTitle: String) -> String {
		acceptTitle == chromeAdd ? "Add \u{201C}\(extensionName)\u{201D} to Pane?" : curlyQuoted(chromeTitle)
	}

	public static func acceptTitle(_ chromeAcceptTitle: String) -> String {
		chromeAcceptTitle == chromeAdd ? "Add to Pane" : chromeAcceptTitle
	}

	public static func removeTitle(_ extensionName: String) -> String {
		"Remove \u{201C}\(extensionName)\u{201D}?"
	}

	// Chrome's prompt titles quote the name straight (Add "Dark Reader"?):
	// the first and the last straight quote turn curly.
	public static func curlyQuoted(_ title: String) -> String {
		guard let open = title.firstIndex(of: "\""), let close = title.lastIndex(of: "\""), open < close else {
			return title
		}

		return title.replacingCharacters(in: close ... close, with: "\u{201D}").replacingCharacters(in: open ... open, with: "\u{201C}")
	}

	// Report abuse, ticked as the extension goes: the Web Store's report
	// page, as Chrome's dialog opens it.
	public static func reportAbuseURL(_ extensionID: String) -> URL? {
		URL(string: "https://chrome.google.com/webstore/report/\(extensionID)?utm_source=chrome-remove-extension-dialog")
	}
}
