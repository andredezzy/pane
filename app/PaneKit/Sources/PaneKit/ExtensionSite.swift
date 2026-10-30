// A site an extension may read and change on, as its Details list writes it
// (Paper: 09.16): Chrome's pattern ("*://example.com/*"), the list's
// constant path dropped, and the scheme when it is any.
public enum ExtensionSite {
	public static func title(_ pattern: String) -> String {
		var title = Substring(pattern)

		if title.hasPrefix("*://") {
			title = title.dropFirst(4)
		}

		if title.hasSuffix("/*") {
			title = title.dropLast(2)
		}

		return String(title)
	}
}
