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
}
