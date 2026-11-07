import Foundation

// What the address field loads for what the user typed: a web URL as is, a
// bare host ("example.com", "localhost:3000") over https, anything else as a
// Google search. Mirrors the Electron app's normalizeUrl.
public enum AddressInput {
	public static let newTabURL = URL(string: "https://www.google.com")!

	public static func url(for input: String) -> URL? {
		let text = input.trimmingCharacters(in: .whitespacesAndNewlines)

		if text.isEmpty {
			return nil
		}

		if text.hasPrefix("http://") || text.hasPrefix("https://") {
			return URL(string: text)
		}

		if text.hasPrefix("localhost") || (text.contains(".") && !text.contains(" ")) {
			return URL(string: "https://\(text)")
		}

		var search = URLComponents(string: "https://www.google.com/search")!
		search.queryItems = [URLQueryItem(name: "q", value: text)]

		return search.url
	}

	// The field at rest: the domain (without www.) and the page's title, for a web
	// address; nil for any other text, which shows as it is.
	public static func label(for text: String, title: String) -> (domain: String, title: String)? {
		guard let url = URL(string: text), ["http", "https"].contains(url.scheme), var host = url.host(), !host.isEmpty else {
			return nil
		}

		if host.hasPrefix("www.") {
			host.removeFirst(4)
		}

		let title = title.trimmingCharacters(in: .whitespaces)

		return (host, title == text ? "" : title)
	}
}
