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
	// DevTools docked to a tab, by tab id.
	private var devTools: [String: WebContents] = [:]
	// When each tab's renderer last died, to tell a crash loop from a crash.
	private var lastCrash: [String: Date] = [:]
	// Built on first use: it needs the window.
	private(set) lazy var dialogs = DialogPresenter(window: browserWindow.window) { [weak self] in
		self?.model.activeTabID
	}
	// The page context menu last built, kept for scenarios, and whether it is
	// on screen.
	private(set) var lastContextMenu: NSMenu?
	private(set) var contextMenuOpen = false
	// Shows extension action popups.
	let extensionPopover = ExtensionPopover()
	// The window's SwiftUI root, where toolbar buttons anchor popups.
	weak var hostingView: NSView?
	private var tabIDs: [ObjectIdentifier: String] = [:]
	// The tab id the engine's next inserted tab gets (a tab the app opens).
	private var pendingTabID: String?
	// Profiles whose engine data is still being deleted.
	private(set) var pendingDeletions: Set<String> = []
	// Each profile's proxy and WebRTC policy.
	let network: ProfileNetwork

	init(model: BrowserModel, application: BrowserApplication) {
		self.model = model
		self.application = application
		network = ProfileNetwork(model: model)
		browserWindow = BrowserWindow(contentRect: NSRect(x: 0, y: 0, width: 1280, height: 800))
		super.init()
		browserWindow.delegate = self
		model.proxyDidChange = { [weak self] profileID in
			self?.proxyDidChange(profileID)
		}

		// The sidebar shows each proxy's state from launch, as the Electron
		// app tested them.
		for profile in model.profiles where profile.proxy != nil {
			network.test(profile.id)
		}
	}

	private func proxyDidChange(_ profileID: String) {
		if let context = contexts[profileID] {
			Task { await network.apply(to: context) }
		}

		network.test(profileID)
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

			guard let context else {
				contextWaiters[profileID] = nil
				NSLog("Pane: could not open profile %@: %@", profileID, String(describing: error))
				return
			}

			context.delegate = self

			// The proxy goes on before any page of the profile loads.
			Task { [weak self] in
				guard let self else {
					return
				}

				await network.apply(to: context)
				let waiters = contextWaiters.removeValue(forKey: profileID) ?? []
				contexts[profileID] = context
				refreshExtensions(profileID)
				waiters.forEach { $0(context) }
			}
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
			devTools[previous]?.isVisible = false
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

	// A loaded tab goes once its page agrees to close (beforeunload may keep
	// it); the engine's didRemoveTab then takes it out. An unloaded tab goes
	// at once.
	func closeTab(_ tabID: String) {
		if let page = pages[tabID] {
			page.close()
			return
		}

		let wasActive = model.activeTabID == tabID
		let profileID = model.profileID(ofTab: tabID)
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
		page.closeImmediately()
		devTools[tabID] = nil
		model.setLoaded(tabID, false)
	}

	// Removes the profile now; its engine data goes once the engine confirms.
	// A profile not opened this session is opened first, so data from earlier
	// sessions goes too.
	func deleteProfile(_ profileID: String) {
		unloadProfile(profileID)
		model.removeProfile(profileID)
		pendingDeletions.insert(profileID)
		network.stop(profileID)
		model.proxyStatus[profileID] = nil

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
		let docked = model.activeTabID.flatMap { devTools[$0] }
		pageHost.show(page, devTools: docked)
		page.isVisible = true
		docked?.isVisible = true
		dialogs.selectionChanged()
		// Actions show their state for the selected tab.
		refreshExtensions(profileID)
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
		if let tabID = model.activeTabID, model.crashedTabIDs.contains(tabID) {
			reloadCrashed(tabID)
			return
		}

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

		// The page closed: the app closed it (once beforeunload agreed), or it
		// closed itself (window.close()). Either way its tab goes too. Pages
		// the app releases are unmapped first.
		tabIDs[ObjectIdentifier(tab)] = nil
		pages[tabID] = nil
		devTools[tabID] = nil
		tab.nativeView.removeFromSuperview()

		let wasActive = model.activeTabID == tabID
		let profileID = model.profileID(ofTab: tabID)
		model.removeTab(tabID)

		if wasActive {
			selectAfterClosing(inProfile: profileID)
		}
	}

	// Chrome made another tab active: an extension's tabs.update or
	// tabs.highlight, say. The app selects it too, unless the selected tab
	// just closed: then the app picks the next one itself (selectAfterClosing),
	// and Chrome's pick, which comes first, is dropped.
	func browserWindow(_ window: BrowserWindow, didActivateTab tab: WebContents) {
		guard let tabID = tabID(of: tab), model.activeTabID != tabID else {
			return
		}

		let previous = model.activeTabID

		DispatchQueue.main.async { [weak self] in
			guard
				let self,
				model.activeTabID == previous,
				previous.map({ model.profileID(ofTab: $0) != nil }) ?? true,
				pages[tabID] != nil
			else {
				return
			}

			select(tabID: tabID)
		}
	}

	// A page's fullscreen fills the window: the sidebar and toolbar step aside,
	// and the window enters full screen as Chrome's does.
	func browserWindow(_ window: BrowserWindow, tab: WebContents, didChangeFullscreen fullscreen: Bool) {
		state.pageFullscreen = fullscreen

		let isFullScreen = window.window.styleMask.contains(.fullScreen)

		if fullscreen != isFullScreen {
			window.window.toggleFullScreen(nil)
		}
	}

	func browserWindow(_ window: BrowserWindow, run chooser: FileChooser) {
		dialogs.enqueue(.fileChooser(chooser), tabID: nil)
	}

	func browserWindow(_ window: BrowserWindow, didDismiss chooser: FileChooser) {
		dialogs.withdraw(chooser)
	}

	func browserWindow(_ window: BrowserWindow, showExclusiveAccessHint hint: String?) {
		state.exclusiveAccessHint = hint
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

		model.setLoading(tabID, progress: nil)

		// The first crash reloads the page, as a slept tab wakes. A page that
		// dies again within a minute shows the sad tab instead of looping.
		let now = Date()
		let looping = lastCrash[tabID].map { now.timeIntervalSince($0) < 60 } ?? false
		lastCrash[tabID] = now

		if looping {
			model.setCrashed(tabID, true)
		} else if model.activeTabID == tabID {
			webContents.reload()
		} else {
			model.setCrashed(tabID, true)
		}
	}

	// MARK: Chrome's dialogs and menus (PaneDialogs.h)

	func webContents(_ webContents: WebContents, run dialog: JavaScriptDialog) {
		dialogs.enqueue(.javaScript(dialog), tabID: tabID(of: webContents))
	}

	func webContents(_ webContents: WebContents, requestPermission request: PermissionRequest) {
		dialogs.enqueue(.permission(request), tabID: tabID(of: webContents))
	}

	// The profile's proxy signs in with its stored credentials; anything
	// else (a site, or a proxy refusing them) asks the user.
	func webContents(_ webContents: WebContents, authenticate challenge: AuthenticationChallenge) {
		if let profileID = webContents.context?.identifier,
		   let credentials = network.credentials(for: challenge, profileID: profileID) {
			challenge.provide(username: credentials.username, password: credentials.password)
			return
		}

		dialogs.enqueue(.authentication(challenge), tabID: tabID(of: webContents))
	}

	func webContents(_ webContents: WebContents, openExternalProtocol request: ExternalProtocolRequest) {
		dialogs.enqueue(.externalProtocol(request), tabID: tabID(of: webContents))
	}

	func webContents(_ webContents: WebContents, becameUnresponsive page: UnresponsivePage) {
		dialogs.enqueue(.unresponsive(page), tabID: tabID(of: webContents))
	}

	func webContents(_ webContents: WebContents, didDismissRequest request: Any) {
		dialogs.withdraw(request as AnyObject)
	}

	func webContents(_ webContents: WebContents, show context: ContextMenu) {
		guard tabID(of: webContents) == model.activeTabID else {
			return
		}

		let menu = PageContextMenu.build(context, page: webContents, controller: self)
		lastContextMenu = menu
		let view = webContents.nativeView
		let point = NSPoint(
			x: context.location.x,
			y: view.isFlipped ? context.location.y : view.bounds.height - context.location.y
		)

		// Shown once the engine's task returns: the menu runs its own loop.
		DispatchQueue.main.async { [weak self] in
			self?.contextMenuOpen = true
			menu.popUp(positioning: nil, at: point, in: view)
			self?.contextMenuOpen = false
		}
	}

	func webContentsWillPrint(_ webContents: WebContents) {
		state.printRequests += 1
	}

	func webContents(_ webContents: WebContents, didChangePictureInPicture active: Bool) {
		guard let tabID = tabID(of: webContents) else {
			return
		}

		model.setPictureInPicture(tabID, active)
	}

	func webContents(_ webContents: WebContents, didChangeDevTools tools: WebContents?) {
		guard let tabID = tabID(of: webContents) else {
			return
		}

		if let previous = devTools[tabID], previous !== tools {
			previous.isVisible = false
		}

		devTools[tabID] = tools
		model.setDevToolsOpen(tabID, tools != nil)

		if model.activeTabID == tabID {
			pageHost.show(webContents, devTools: tools)
			tools?.isVisible = true
		}
	}
}

// MARK: - Actions behind Chrome's dialogs and menus

extension BrowserController {
	// "Open Link in New Tab": a background tab after the page's, in its profile.
	func openInNewTab(_ url: URL, besides page: WebContents) {
		guard let sourceID = tabID(of: page), let profileID = model.profileID(ofTab: sourceID) else {
			return
		}

		let tab = Tab(id: UUID().uuidString.lowercased(), url: url.absoluteString)
		model.insertTab(tab, inProfile: profileID, after: sourceID)

		withContext(profileID) { [weak self] context in
			self?.openPage(tabID: tab.id, url: url, context: context, foreground: false)
		}
	}

	func toggleDevTools() {
		guard let page = activePage else {
			return
		}

		page.isDevToolsOpen ? page.closeDevTools() : page.showDevTools()
	}

	func printPage() {
		activePage?.printPage()
	}

	// The sad tab's Reload, or any reload of a crashed tab.
	func reloadCrashed(_ tabID: String) {
		model.setCrashed(tabID, false)
		lastCrash[tabID] = nil
		pages[tabID]?.reload()
	}
}

// MARK: - BrowserContextDelegate

extension BrowserController: BrowserContextDelegate {
	func browserContext(_ context: BrowserContext, didUpdate download: Download) {
		NotificationCenter.default.post(name: .paneDownloadDidUpdate, object: download)
	}

	func browserContextDidChangeExtensions(_ context: BrowserContext) {
		refreshExtensions(context.identifier)
	}

	func browserContext(_ context: BrowserContext, confirmExtensionInstall request: ExtensionInstallRequest) {
		dialogs.enqueue(.extensionInstall(request), tabID: nil)
	}

	func browserContext(_ context: BrowserContext, didInstall installed: Extension) {
		state.installedExtension = installed.name
		refreshExtensions(context.identifier)
	}

	func browserContext(_ context: BrowserContext, show popup: ExtensionPopup, in window: BrowserWindow) {
		guard let hostingView else {
			popup.close()
			return
		}

		let anchor = state.actionAnchors[popup.extensionIdentifier]
			?? state.actionAnchors[WindowState.extensionsMenuAnchor]
			?? NSRect(x: hostingView.bounds.maxX - 48, y: 8, width: 26, height: 26)
		extensionPopover.show(popup, relativeTo: anchor, of: hostingView)
	}
}

// MARK: - Extensions

extension BrowserController {
	// Opens the profile's engine context if it is not open yet, so its
	// extensions can be listed and managed.
	func openContextIfNeeded(profileID: String) {
		withContext(profileID) { [weak self] _ in
			self?.refreshExtensions(profileID)
		}
	}

	func refreshExtensions(_ profileID: String) {
		guard let context = context(for: profileID) else {
			return
		}

		model.setExtensions(profileID, context.extensions.map(ExtensionInfo.init))
	}

	// The extension's toolbar action in the active profile: its popup, or
	// chrome.action.onClicked.
	func runExtensionAction(_ extensionID: String) {
		guard let profileID = model.activeProfileID, let context = context(for: profileID) else {
			return
		}

		context.runAction(ofExtension: extensionID, in: browserWindow)
	}

	// Installs from the Web Store into a profile; the engine asks first.
	func installExtension(_ identifier: String, inProfile profileID: String) {
		withContext(profileID) { [weak self] context in
			guard let self else {
				return
			}

			context.installExtension(identifier: identifier, window: browserWindow)
		}
	}

	func setExtension(_ extensionID: String, enabled: Bool, inProfile profileID: String) {
		context(for: profileID)?.setEnabled(enabled, forExtension: extensionID)
	}

	func setExtension(_ extensionID: String, pinned: Bool, inProfile profileID: String) {
		context(for: profileID)?.setPinned(pinned, forExtension: extensionID)
	}

	func uninstallExtension(_ extensionID: String, inProfile profileID: String) {
		context(for: profileID)?.uninstallExtension(extensionID)
	}

	func updateExtensions(inProfile profileID: String) {
		context(for: profileID)?.updateExtensions()
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
