import Foundation
import PaneCore
import PaneKit

// A profile's Google session (apps/veil profile/google/sessions.ts and
// sign-out.ts): whether it is signed in, signing out, and the cookies a real
// Chrome's sign-in brings back.
@MainActor
enum GoogleSession {
	// Signed in when Google set an account cookie on one of its domains.
	static func isSignedIn(_ context: BrowserContext) async -> Bool {
		await context.cookies().contains { GoogleAccounts.isAuthCookie(name: $0.name, domain: $0.domain) }
	}

	// Removes every Google and YouTube cookie, and the storage Google's
	// account widgets read, which would otherwise leave accounts.google.com
	// looping on error #2014. Returns how many cookies went.
	@discardableResult
	static func signOut(_ context: BrowserContext) async -> Int {
		var removed = 0

		for cookie in await context.cookies() where GoogleAccounts.isCookieDomain(cookie.domain) {
			if await context.deleteCookie(cookie) {
				removed += 1
			}
		}

		await context.clearSiteData(origins: GoogleAccounts.storageOrigins.compactMap(URL.init(string:)))

		return removed
	}

	// Stores the cookies a real Chrome signed in with. Returns how many the
	// profile took.
	static func importCookies(_ cookies: [ChromeAuth.Cookie], into context: BrowserContext) async -> Int {
		var imported = 0

		for cookie in cookies {
			let sameSite: CookieSameSite = switch cookie.sameSite {
			case "None": .none
			case "Lax": .lax
			case "Strict": .strict
			default: .unspecified
			}

			let stored = await context.setCookie(Cookie(
				name: cookie.name,
				value: cookie.value,
				domain: cookie.domain,
				path: cookie.path,
				secure: cookie.secure,
				httpOnly: cookie.httpOnly,
				expirationDate: cookie.expires > 0 ? Date(timeIntervalSince1970: cookie.expires) : nil,
				sameSite: sameSite
			))

			if stored {
				imported += 1
			} else {
				NSLog("Pane: the profile refused the Google cookie %@", cookie.name)
			}
		}

		return imported
	}
}
