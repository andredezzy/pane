import PaneCore

// Which engine page is which tab's: one live page (WebContents) per loaded
// tab, both ways, and the DevTools docked to each. It only keeps the
// mapping; BrowserController decides when a page comes and goes.
@MainActor
final class PageRegistry {
	private var pages: [String: WebContents] = [:]
	private var tabIDs: [ObjectIdentifier: String] = [:]
	// DevTools docked to a tab, by tab id.
	private var devTools: [String: WebContents] = [:]
	// The tab id the engine's next inserted page gets: a tab the app opens.
	private(set) var pendingTabID: String?

	func page(for tabID: String) -> WebContents? {
		pages[tabID]
	}

	func tabID(of page: WebContents) -> String? {
		tabIDs[ObjectIdentifier(page)]
	}

	func devTools(for tabID: String) -> WebContents? {
		devTools[tabID]
	}

	// Runs `open` with `tabID` pending, so the page it inserts is that
	// tab's.
	func opening<Result>(_ tabID: String, _ open: () -> Result) -> Result {
		pendingTabID = tabID
		defer { pendingTabID = nil }

		return open()
	}

	func adopt(_ page: WebContents, tabID: String) {
		pages[tabID] = page
		tabIDs[ObjectIdentifier(page)] = tabID
	}

	// The tab's DevTools, docked or gone; the ones they replace, if any.
	@discardableResult
	func setDevTools(_ tools: WebContents?, for tabID: String) -> WebContents? {
		let previous = devTools[tabID]
		devTools[tabID] = tools

		return previous === tools ? nil : previous
	}

	// The tab's page leaves the registry, its DevTools with it.
	@discardableResult
	func remove(tabID: String) -> WebContents? {
		guard let page = pages.removeValue(forKey: tabID) else {
			return nil
		}

		tabIDs[ObjectIdentifier(page)] = nil
		devTools[tabID] = nil

		return page
	}
}
