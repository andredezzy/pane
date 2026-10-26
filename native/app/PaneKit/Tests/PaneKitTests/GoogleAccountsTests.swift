import Foundation
import PaneKit
import Testing

@Suite("GoogleAccounts")
struct GoogleAccountsTests {
	@Test("cookie domains: Google and YouTube on any ccTLD, nothing that only ends like them")
	func cookieDomains() {
		for domain in [".google.com", "google.com", "accounts.google.com", ".google.co.uk", "youtube.de", ".google.com.au", ".YouTube.com"] {
			#expect(GoogleAccounts.isCookieDomain(domain), "\(domain)")
		}

		for domain in ["notgoogle.com", ".notyoutube.com", "google.com.evil.example", "google", "example.com", ""] {
			#expect(!GoogleAccounts.isCookieDomain(domain), "\(domain)")
		}
	}

	@Test("Google URLs: any subdomain and ccTLD, never a host smuggled after a delimiter")
	func googleURLs() {
		for url in ["https://www.google.com/", "https://mail.google.com/mail/u/0", "http://youtube.com", "https://www.google.co.uk?q=1", "https://google.com:443/#x"] {
			#expect(GoogleAccounts.isGoogleURL(url), "\(url)")
		}

		for url in ["https://evil.com#.google.com", "https://evil.com/?.google.com", "https://evil.com/www.google.com", "https://notgoogle.com/", "ftp://google.com/"] {
			#expect(!GoogleAccounts.isGoogleURL(url), "\(url)")
		}
	}

	@Test("only auth cookies on a Google domain mean signed in")
	func authCookies() {
		#expect(GoogleAccounts.isAuthCookie(name: "SID", domain: ".google.com"))
		#expect(GoogleAccounts.isAuthCookie(name: "__Secure-3PSID", domain: ".youtube.com"))
		#expect(!GoogleAccounts.isAuthCookie(name: "NID", domain: ".google.com"))
		#expect(!GoogleAccounts.isAuthCookie(name: "SID", domain: ".notgoogle.com"))
	}

	@Test("the rejection page is accounts.google.com's /rejected, and continues to a Google https page")
	func rejection() {
		let rejected = URL(string: "https://accounts.google.com/v3/signin/rejected?continue=https://mail.google.com/mail/&flowName=GlifWebSignIn")!

		#expect(GoogleAccounts.rejection(rejected) == URL(string: "https://mail.google.com/mail/"))
		#expect(GoogleAccounts.rejection(URL(string: "https://accounts.google.com/signin/rejected")!) == URL(string: "https://www.google.com/"))
		#expect(GoogleAccounts.rejection(URL(string: "https://accounts.google.com/v3/signin/identifier")!) == nil)
	}

	@Test("a rejection on another host is not one, and a foreign continue goes to Google instead")
	func rejectionSafety() {
		#expect(GoogleAccounts.rejection(URL(string: "https://evil.com/accounts.google.com/rejected")!) == nil)
		#expect(GoogleAccounts.rejection(URL(string: "https://accounts.google.com.evil.com/rejected")!) == nil)

		for next in ["https://evil.com/", "http://mail.google.com/", "https://evil.com#.google.com", "javascript:alert(1)"] {
			let url = URL(string: "https://accounts.google.com/rejected?continue=\(next.addingPercentEncoding(withAllowedCharacters: .alphanumerics)!)")!

			#expect(GoogleAccounts.rejection(url) == URL(string: "https://www.google.com/"), "\(next)")
		}
	}

	@Test("an overridden accounts origin matches its host and port only")
	func overriddenOrigin() {
		let origin = URL(string: "http://127.0.0.1:4100")!

		#expect(GoogleAccounts.rejection(URL(string: "http://127.0.0.1:4100/v3/signin/rejected?continue=https://www.google.com/x")!, accountsOrigin: origin) == URL(string: "https://www.google.com/x"))
		#expect(GoogleAccounts.rejection(URL(string: "http://127.0.0.1:4101/v3/signin/rejected")!, accountsOrigin: origin) == nil)
		#expect(GoogleAccounts.rejection(URL(string: "https://accounts.google.com/v3/signin/rejected")!, accountsOrigin: origin) == nil)
	}

	@Test("the login URL continues where the rejection was going")
	func loginURL() {
		let url = GoogleAccounts.loginURL(continue: URL(string: "https://mail.google.com/mail/?a=1&b=2")!)

		#expect(url.absoluteString == "https://accounts.google.com/ServiceLogin?continue=https://mail.google.com/mail/?a%3D1%26b%3D2")
	}
}
