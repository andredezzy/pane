import Foundation

// Google sign-in's rules (apps/veil profile/google): which cookies and pages
// are Google's, when Google has rejected the engine, and what a profile's
// Google session is made of.
public enum GoogleAccounts {
	// Anchored and ccTLD-aware (domains.ts): google.com, .google.co.uk,
	// youtube.de, never notgoogle.com. Every real Google ccTLD suffix is two or
	// three letters, or co.xx / com.xx.
	public static func isCookieDomain(_ domain: String) -> Bool {
		domain.wholeMatch(of: /(?i)(?:.*\.)?(?:google|youtube)\.[a-z]{2,3}(?:\.[a-z]{2,3})?/) != nil
	}

	// A Google or YouTube page on any subdomain and ccTLD. The subdomain
	// excludes / ? # so a URL delimiter cannot smuggle in a foreign host
	// ("https://evil.com#.google.com" is evil.com's).
	public static func isGoogleURL(_ url: String) -> Bool {
		url.prefixMatch(of: /(?i)https?:\/\/(?:[^\/?#]*\.)?(?:google|youtube)\.[a-z]{2,3}(?:\.[a-z]{2,3})?(?::\d+)?(?:[\/?#]|$)/) != nil
	}

	// Cookies Google sets only for a signed-in account; anonymous visitors
	// get NID or CONSENT, never these.
	public static let authCookieNames: Set<String> = [
		"SID", "HSID", "SSID", "APISID", "SAPISID",
		"__Secure-1PSID", "__Secure-3PSID", "__Secure-1PAPISID", "__Secure-3PAPISID",
	]

	// Whether cookies named `name` on `domain` mean the profile is signed in.
	public static func isAuthCookie(name: String, domain: String) -> Bool {
		authCookieNames.contains(name) && isCookieDomain(domain)
	}

	// The origins whose storage Google's account widgets read (sign-out.ts):
	// cleared with the cookies, or accounts.google.com loops on error #2014
	// after a sign-out.
	public static let storageOrigins = [
		"https://accounts.google.com",
		"https://myaccount.google.com",
		"https://www.google.com",
		"https://google.com",
		"https://mail.google.com",
		"https://drive.google.com",
		"https://docs.google.com",
		"https://calendar.google.com",
		"https://photos.google.com",
		"https://play.google.com",
		"https://ogs.google.com",
		"https://apis.google.com",
		"https://www.youtube.com",
		"https://youtube.com",
		"https://m.youtube.com",
		"https://studio.youtube.com",
		"https://music.youtube.com",
	]

	public static let accountsOrigin = URL(string: "https://accounts.google.com")!

	// Google's "this browser may not be secure" page: a page of the accounts
	// origin whose path has /rejected. Matched on the host (and port), never
	// as a substring, so evil.com/accounts.google.com/rejected is not it.
	// Returns the page to continue to after signing in, which is only ever a
	// Google https page; `continue` is attacker-influenceable.
	public static func rejection(_ url: URL, accountsOrigin: URL = accountsOrigin) -> URL? {
		guard
			let host = url.host(), host.lowercased() == accountsOrigin.host()?.lowercased(),
			url.port == accountsOrigin.port,
			url.path().contains("/rejected")
		else {
			return nil
		}

		let raw = URLComponents(url: url, resolvingAgainstBaseURL: false)?
			.queryItems?.first { $0.name == "continue" }?.value ?? ""

		if raw.hasPrefix("https://"), isGoogleURL(raw), let next = URL(string: raw) {
			return next
		}

		return URL(string: "https://www.google.com/")!
	}

	// Where the real Chrome signs in, returning to `continue`.
	public static func loginURL(continue next: URL, accountsOrigin: URL = accountsOrigin) -> URL {
		var components = URLComponents(url: accountsOrigin.appending(path: "ServiceLogin"), resolvingAgainstBaseURL: false)!
		components.queryItems = [URLQueryItem(name: "continue", value: next.absoluteString)]

		return components.url!
	}
}
