import PaneKit
import Testing

@Suite("extension sites")
struct ExtensionSiteTests {
	@Test("a site reads without the list's path, and without its scheme when any")
	func title() {
		#expect(ExtensionSite.title("*://portal.northwind-studio.com/*") == "portal.northwind-studio.com")
		#expect(ExtensionSite.title("*://*.example.com/*") == "*.example.com")
		#expect(ExtensionSite.title("https://example.com/*") == "https://example.com")
		#expect(ExtensionSite.title("http://127.0.0.1:8080/*") == "http://127.0.0.1:8080")
	}
}
