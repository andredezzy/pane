import AppKit
import PaneCore
import PaneKit

// Joins the model and the engine: opens a profile's BrowserContext on first
// use, keeps one live page (WebContents) per loaded tab, shows the selected
// tab, and turns the engine's reports into model changes.
@MainActor
final class BrowserController: NSObject {
	let model: BrowserModel
	let state = WindowState()
	let browserWindow: BrowserWindow
	// Holds the selected tab's page view.
	let pageHost = PageHostView()

	private let application: BrowserApplication
	private var contexts: [String: BrowserContext] = [:]
	private var contextWaiters: [String: [(BrowserContext) -> Void]] = [:]
	private var pages: [String: WebContents] = [:]
	private var tabIDs: [ObjectIdentifier: String] = [:]
	// The tab id the engine's next inserted tab gets (a tab the app opens).
	private var pendingTabID: String?
	// Profiles whose engine data is still being deleted.
	private(set) var pendingDeletions: Set<String> = []

	init(model: BrowserModel, application: BrowserApplication) {
		self.model = model
		self.application = application
		browserWindow = BrowserWindow(contentRect: NSRect(x: 0, y: 0, width: 1280, height: 820))
		super.init()
		browserWindow.delegate = self
	}

	// MARK: Contexts

	private func withContext(_ profileID: String, _ body: @escaping (BrowserContext) -> Void) {
		if let context = contexts[profileID] {
			body(context)
			return
		}

		if contextWaiters[profileID] != nil {
			contextWaiters[profileID]?.append(body)
			return
		}

		contextWaiters[profileID] = [body]
		application.openContext(identifier: profileID) { [weak self] context, error in
			guard let self else {
				return
			}

			let waiters = contextWaiters.removeValue(forKey: profileID) ?? []

			guard let context else {
				NSLog("Pane: could not open profile %@: %@", profileID, String(describing: error))
				return
			}

			context.delegate = self
			contexts[profileID] = context
			waiters.forEach { $0(context) }
		}
	}

	func context(for profileID: String) -> BrowserContext? {
		contexts[profileID]
	}

	// MARK: Tabs

	var activePage: WebContents? {
		model.activeTabID.flatMap { pages[$0] }
	}

	func page(for tabID: String) -> WebContents? {
		pages[tabID]
	}

	// Selects a tab, loading its page if it has none.
	func select(tabID: String) {
		guard let tab = model.tab(tabID), let profileID = model.profileID(ofTab: tabID) else {
			return
		}

		let previous = model.activeTabID
		model.select(tabID: tabID)
		model.page = .browser
		model.expandedProfileIDs.insert(profileID)

		if previous != tabID, let previous, let page = pages[previous] {
			page.isVisible = false
		}

		if let page = pages[tabID] {
			show(page, profileID: profileID)
			return
		}

		withContext(profileID) { [weak self] context in
			guard let self, model.activeTabID == tabID, pages[tabID] == nil else {
				return
			}

			let url = URL(string: tab.url).flatMap { $0.scheme == nil ? nil : $0 }
			openPage(tabID: tabID, url: url, context: context, foreground: true)
		}
	}

	// A new tab in `profileID`, selected. Without a URL it opens the start page
	// and the address field takes focus.
	@discardableResult
	func openTab(inProfile profileID: String, url: URL? = nil) -> String {
		let tab = Tab(id: UUID().uuidString.lowercased(), url: (url ?? AddressInput.newTabURL).absoluteString)
		model.insertTab(tab, inProfile: profileID, after: nil)
		select(tabID: tab.id)

		if url == nil {
			state.focusAddressField()
		}

		return tab.id
	}

	func newTabInActiveProfile() {
		guard let profileID = model.activeProfileID ?? model.profiles.first?.id else {
			return
		}

		openTab(inProfile: profileID)
	}

	func closeTab(_ tabID: String) {
		let page = pages.removeValue(forKey: tabID)
		let wasActive = model.activeTabID == tabID
		let profileID = model.profileID(ofTab: tabID)

		if let page {
			tabIDs[ObjectIdentifier(page)] = nil
			page.nativeView.removeFromSuperview()
			page.close()
		}

		model.removeTab(tabID)

		if wasActive {
			selectAfterClosing(inProfile: profileID)
		}
	}

	private func selectAfterClosing(inProfile profileID: String?) {
		let candidates = profileID.flatMap(model.profile)?.tabs ?? []

		if let next = mostRecentTab(candidates, history: model.history.mru) {
			select(tabID: next.id)
		} else {
			pageHost.show(nil)
			model.select(tabID: nil)
			model.setActiveProfile(profileID)
		}
	}

	func closeActiveTab() {
		if let tabID = model.activeTabID {
			closeTab(tabID)
		}
	}

	func reopenClosedTab() {
		guard let closed = model.popClosedTab(), model.profile(closed.profileID) != nil else {
			return
		}

		openTab(inProfile: closed.profileID, url: URL(string: closed.url))
	}

	// ⌘1…⌘9: the active profile's tabs by position.
	func selectTab(atIndex index: Int) {
		guard
			let profile = model.activeProfileID.flatMap(model.profile),
			profile.tabs.indices.contains(index)
		else {
			return
		}

		select(tabID: profile.tabs[index].id)
	}

	// Releases every page of a profile; its tabs stay (CONTEXT.md: Unload).
	func unloadProfile(_ profileID: String) {
		guard let profile = model.profile(profileID) else {
			return
		}

		for tab in profile.tabs {
			release(tabID: tab.id)
		}

		if model.activeProfileID == profileID {
			pageHost.show(nil)
			model.select(tabID: nil)
			model.setActiveProfile(nil)
		}
	}

	// Drops a tab's page; the tab stays in the sidebar, dimmed. The page is
	// unmapped before it closes, so its removal report is not a tab closing.
	func release(tabID: String) {
		guard let page = pages.removeValue(forKey: tabID) else {
			return
		}

		tabIDs[ObjectIdentifier(page)] = nil
		page.nativeView.removeFromSuperview()
		page.close()
		model.setLoaded(tabID, false)
	}

	// Removes the profile now; its engine data goes once the engine confirms.
	// A profile not opened this session is opened first, so data from earlier
	// sessions goes too.
	func deleteProfile(_ profileID: String) {
		unloadProfile(profileID)
		model.removeProfile(profileID)
		pendingDeletions.insert(profileID)

		withContext(profileID) { [weak self] context in
			guard let self else {
				return
			}

			contexts[profileID] = nil
			application.deleteContext(context) { [weak self] _ in
				self?.pendingDeletions.remove(profileID)
			}
		}
	}

	// MARK: Pages

	private func openPage(tabID: String, url: URL?, context: BrowserContext, foreground: Bool) {
		browserWindow.activeContext = context
		pendingTabID = tabID
		let page = browserWindow.openTab(url: url, context: context, foreground: foreground)
		pendingTabID = nil

		if let page, pages[tabID] == nil {
			adopt(page, tabID: tabID)
		}

		if model.activeTabID == tabID, let page = pages[tabID] {
			show(page, profileID: context.identifier)
		}
	}

	private func adopt(_ page: WebContents, tabID: String) {
		pages[tabID] = page
		tabIDs[ObjectIdentifier(page)] = tabID
		page.delegate = self
		model.setLoaded(tabID, true)
		model.setNavigation(tabID, .init(canGoBack: page.canGoBack, canGoForward: page.canGoForward))
	}

	private func show(_ page: WebContents, profileID: String) {
		if let context = contexts[profileID] {
			browserWindow.activeContext = context
		}

		browserWindow.activateTab(page)
		pageHost.show(page)
		page.isVisible = true
		browserWindow.window.title = page.title.isEmpty ? "Pane" : page.title
	}

	private func tabID(of page: WebContents) -> String? {
		tabIDs[ObjectIdentifier(page)]
	}

	// MARK: Navigation

	func navigate(to input: String) {
		guard let url = AddressInput.url(for: input) else {
			return
		}

		if let page = activePage {
			page.load(url)
			page.focus()
		} else if let tabID = model.activeTabID {
			model.updateTab(tabID) { $0.url = url.absoluteString }
			select(tabID: tabID)
		} else if let profileID = model.activeProfileID ?? model.profiles.first?.id {
			openTab(inProfile: profileID, url: url)
		}
	}

	func goBack() {
		activePage?.goBack()
	}

	func goForward() {
		activePage?.goForward()
	}

	func reload() {
		guard let page = activePage else {
			model.activeTabID.map(select)
			return
		}

		page.reload()
	}

	func stop() {
		activePage?.stopLoading()
	}

	func zoomIn() {
		activePage?.zoomIn()
	}

	func zoomOut() {
		activePage?.zoomOut()
	}

	func resetZoom() {
		activePage?.resetZoom()
	}

	// MARK: Find

	func showFind() {
		state.find.visible = true
		state.focusFindField()
	}

	func find(_ text: String, forward: Bool = true) {
		state.find.text = text

		if text.isEmpty {
			activePage?.stopFinding()
			state.find.matches = 0
			state.find.activeMatch = 0
		} else {
			activePage?.findText(text, forward: forward)
		}
	}

	func closeFind() {
		state.find = .init()
		activePage?.stopFinding()
		activePage?.focus()
	}
}

// MARK: - BrowserWindowDelegate

extension BrowserController: BrowserWindowDelegate {
	func browserWindow(_ window: BrowserWindow, didInsertTab tab: WebContents, opener: WebContents?, foreground: Bool) {
		if let tabID = pendingTabID {
			adopt(tab, tabID: tabID)
			return
		}

		// A page opened it (a popup, a link in a new tab): a new tab after its
		// opener, in the opener's profile.
		guard let profileID = tab.context?.identifier else {
			return
		}

		let tabID = UUID().uuidString.lowercased()
		model.insertTab(Tab(id: tabID, url: tab.url?.absoluteString ?? ""), inProfile: profileID, after: opener.flatMap(self.tabID))
		adopt(tab, tabID: tabID)

		if foreground {
			select(tabID: tabID)
		} else {
			tab.isVisible = false
		}
	}

	func browserWindow(_ window: BrowserWindow, didRemoveTab tab: WebContents) {
		guard let tabID = tabID(of: tab) else {
			return
		}

		// Still mapped, so the page closed itself (window.close()): its tab goes
		// too. Pages the app closes or releases are unmapped first.
		tabIDs[ObjectIdentifier(tab)] = nil
		pages[tabID] = nil
		tab.nativeView.removeFromSuperview()

		let wasActive = model.activeTabID == tabID
		let profileID = model.profileID(ofTab: tabID)
		model.removeTab(tabID)

		if wasActive {
			selectAfterClosing(inProfile: profileID)
		}
	}

	func browserWindow(_ window: BrowserWindow, tab: WebContents, didChangeFullscreen fullscreen: Bool) {
		let isFullScreen = window.window.styleMask.contains(.fullScreen)

		if fullscreen != isFullScreen {
			window.window.toggleFullScreen(nil)
		}
	}
}

// MARK: - WebContentsDelegate

extension BrowserController: WebContentsDelegate {
	func webContents(_ webContents: WebContents, didChangeTitle title: String) {
		guard let tabID = tabID(of: webContents) else {
			return
		}

		model.updateTab(tabID) { $0.title = title }

		if model.activeTabID == tabID {
			browserWindow.window.title = title.isEmpty ? "Pane" : title
		}
	}

	func webContents(_ webContents: WebContents, didChange url: URL) {
		guard let tabID = tabID(of: webContents) else {
			return
		}

		model.updateTab(tabID) { $0.url = url.absoluteString }
	}

	func webContents(_ webContents: WebContents, didChangeLoading isLoading: Bool, progress: Double) {
		guard let tabID = tabID(of: webContents) else {
			return
		}

		model.setLoading(tabID, progress: isLoading ? progress : nil)
	}

	func webContents(_ webContents: WebContents, didUpdateFavicon favicon: NSImage) {
		guard let tabID = tabID(of: webContents), let dataURL = favicon.pngDataURL else {
			return
		}

		model.updateTab(tabID) { $0.favicon = dataURL }
	}

	func webContentsDidChangeNavigationState(_ webContents: WebContents) {
		guard let tabID = tabID(of: webContents) else {
			return
		}

		model.setNavigation(tabID, .init(canGoBack: webContents.canGoBack, canGoForward: webContents.canGoForward))
	}

	func webContents(_ webContents: WebContents, didFindMatches count: Int, activeMatch: Int, final: Bool) {
		guard tabID(of: webContents) == model.activeTabID else {
			return
		}

		state.find.matches = count
		state.find.activeMatch = activeMatch
	}

	func webContents(_ webContents: WebContents, rendererDidTerminate termination: RendererTermination) {
		guard let tabID = tabID(of: webContents) else {
			return
		}

		// The page is gone; reloading recreates it (CONTEXT.md: like a slept tab).
		model.setLoading(tabID, progress: nil)
		if model.activeTabID == tabID {
			webContents.reload()
		}
	}
}

// MARK: - BrowserContextDelegate

extension BrowserController: BrowserContextDelegate {
	func browserContext(_ context: BrowserContext, didUpdate download: Download) {
		NotificationCenter.default.post(name: .paneDownloadDidUpdate, object: download)
	}
}

extension Notification.Name {
	static let paneDownloadDidUpdate = Notification.Name("PaneDownloadDidUpdate")
}

extension NSImage {
	// A PNG data: URL, the form tabs persist favicons in.
	var pngDataURL: String? {
		guard
			let tiff = tiffRepresentation,
			let bitmap = NSBitmapImageRep(data: tiff),
			let png = bitmap.representation(using: .png, properties: [:])
		else {
			return nil
		}

		return "data:image/png;base64,\(png.base64EncodedString())"
	}
}
