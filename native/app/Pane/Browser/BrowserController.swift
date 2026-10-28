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
	private var contextWaiters: [String: [(BrowserContext?) -> Void]] = [:]
	private var pages: [String: WebContents] = [:]
	// DevTools docked to a tab, by tab id.
	private var devTools: [String: WebContents] = [:]
	// When each tab's renderer last died, to tell a crash loop from a crash.
	private var lastCrash: [String: Date] = [:]
	// Built on first use: it needs the window.
	private(set) lazy var dialogs = DialogPresenter(
		window: browserWindow.window,
		activeTabID: { [weak self] in self?.model.activeTabID },
		isHeld: { [weak self] in self?.interactionAllowed == false }
	)
	// The page context menu last built, kept for scenarios, and whether it is
	// on screen.
	private(set) var lastContextMenu: NSMenu?
	private(set) var contextMenuOpen = false
	// Shows extension action popups.
	let extensionPopover = ExtensionPopover()
	// The window's SwiftUI root, where toolbar buttons anchor popups.
	weak var hostingView: NSView?
	// Quit Pane?'s keys, while it shows.
	var quitKeyMonitor: Any?
	// Esc and Return, while the extensions menu, a more menu, or an extension
	// popup shows.
	private var extensionsKeyMonitor: Any?
	private var tabIDs: [ObjectIdentifier: String] = [:]
	// The tab id the engine's next inserted tab gets (a tab the app opens).
	private var pendingTabID: String?
	// Profiles whose engine data is still being deleted.
	private(set) var pendingDeletions: Set<String> = []
	// The same, on disk until the engine confirms, for the next launch.
	private let deletions: ProfileDeletions
	// Each profile's proxy and WebRTC policy.
	let network: ProfileNetwork
	// The PIN lock.
	let lock: AppLock
	// The update check and download.
	let updater: Updater
	// The URL Pane was started with, while it waits behind the lock.
	private var launchURL: URL?

	init(model: BrowserModel, application: BrowserApplication, lock: AppLock, updater: Updater) {
		self.model = model
		self.application = application
		self.lock = lock
		self.updater = updater
		network = ProfileNetwork(model: model)
		deletions = ProfileDeletions(dataDirectory: model.dataDirectory)
		browserWindow = BrowserWindow(contentRect: NSRect(x: 0, y: 0, width: 1280, height: 800))
		super.init()
		browserWindow.delegate = self
		state.sidebarWidth = CGFloat(model.settings.sidebarWidth)
		model.proxyDidChange = { [weak self] profileID in
			self?.proxyDidChange(profileID)
		}
		model.fingerprintDidChange = { [weak self] profileID in
			self?.fingerprintDidChange(profileID)
		}
		lock.didUnlock = { [weak self] in
			self?.lockDidOpen()
		}
		model.pageDidChange = { [weak self] in
			self?.placeActivePage()
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

	// The Mac's own WebGL strings, for a fingerprint on its platform.
	var hostWebGL: Fingerprint.WebGL? {
		guard let vendor = application.hostWebGLVendor, let renderer = application.hostWebGLRenderer else {
			return nil
		}

		return Fingerprint.WebGL(vendor: vendor, renderer: renderer)
	}

	// A changed fingerprint: the profile's open pages start again in new
	// renderers, which take it; the selected tab loads again.
	private func fingerprintDidChange(_ profileID: String) {
		applyFingerprint(profileID)

		guard contexts[profileID] != nil, let profile = model.profile(profileID) else {
			return
		}

		let selected = model.activeTabID

		for tab in profile.tabs {
			release(tabID: tab.id)
		}

		if let selected, profile.tabs.contains(where: { $0.id == selected }) {
			select(tabID: selected)
		}
	}

	// The profile's fingerprint, or Chrome's own values without one. It goes
	// to the engine before the profile's context opens, since its network
	// context takes the User-Agent as it is created. A change reaches
	// navigations and new renderers at once; open pages keep their renderer's.
	private func applyFingerprint(_ profileID: String) {
		do {
			let json = try model.profile(profileID)?.fingerprint.map { fingerprint in
				try EngineFingerprint(
					fingerprint: fingerprint,
					profileID: profileID,
					engineUserAgent: application.userAgent,
					engineVersion: application.chromiumVersion
				).json()
			}

			try application.setFingerprint(json, forContextIdentifier: profileID)
		} catch {
			NSLog("Pane: could not apply the fingerprint of profile %@: %@", profileID, String(describing: error))
		}
	}

	// MARK: Lock

	// Whether the user may act on the browser. Behind the PIN lock nothing
	// opens, loads, or shows: no tab or page, no menu item but Quit and Check
	// for Updates…, no Chrome dialog or extension popup. Every way in asks
	// this one question (the menu bar, the switcher, a launch URL, the
	// engine's requests), so the lock holds for the mouse as for the keys.
	var interactionAllowed: Bool {
		!lock.isLocked
	}

	// The lock opened: what waited behind it goes on.
	private func lockDidOpen() {
		if let url = launchURL {
			launchURL = nil
			openLaunchURL(url)
		}

		if let page = activePage, let profileID = model.activeProfileID {
			show(page, profileID: profileID)
		}

		dialogs.resume()
	}

	// `Pane https://…` opens the URL as a new tab, in the first profile (made
	// on first launch when there is none), once the lock is open.
	func openLaunchURL(_ url: URL) {
		guard interactionAllowed else {
			launchURL = url
			return
		}

		let profileID = model.profiles.first?.id
			?? model.createProfile(name: "Personal", color: .blue, group: nil)
		model.expandedProfileIDs.insert(profileID)
		openTab(inProfile: profileID, url: url)
	}

	// MARK: Contexts

	// Runs `body` with the profile's context, opening it first if needed;
	// with nil when it would not open, so no caller is left half done.
	private func withContext(_ profileID: String, _ body: @escaping (BrowserContext?) -> Void) {
		if let context = contexts[profileID] {
			body(context)
			return
		}

		if contextWaiters[profileID] != nil {
			contextWaiters[profileID]?.append(body)
			return
		}

		contextWaiters[profileID] = [body]
		applyFingerprint(profileID)
		application.openContext(identifier: profileID) { [weak self] context, error in
			guard let self else {
				return
			}

			guard let context else {
				NSLog("Pane: could not open profile %@: %@", profileID, String(describing: error))
				contextWaiters.removeValue(forKey: profileID)?.forEach { $0(nil) }
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
		guard interactionAllowed, let tab = model.tab(tabID), let profileID = model.profileID(ofTab: tabID) else {
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

			// The profile would not open: the sad tab, whose Reload tries
			// again.
			guard let context else {
				model.setCrashed(tabID, true)
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

		removeTabAndReselect(tabID)
	}

	// The tab goes from the model; when it was selected, the profile's most
	// recent tab takes its place.
	private func removeTabAndReselect(_ tabID: String) {
		let wasActive = model.activeTabID == tabID
		let profileID = model.profileID(ofTab: tabID)
		lastCrash[tabID] = nil
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
		lastCrash[tabID] = nil
		model.setLoaded(tabID, false)
	}

	// Removes the profile now; its engine data goes once the engine confirms.
	// A profile not opened this session is opened first, so data from earlier
	// sessions goes too. Until then deletions.json names it, so a quit, a
	// crash, or a context that would not open leaves the next launch to
	// remove its folder (main.swift).
	func deleteProfile(_ profileID: String) {
		unloadProfile(profileID)
		model.removeProfile(profileID)
		pendingDeletions.insert(profileID)
		deletions.add(profileID)
		network.stop(profileID)
		model.proxyStatus[profileID] = nil

		withContext(profileID) { [weak self] context in
			guard let self else {
				return
			}

			guard let context else {
				pendingDeletions.remove(profileID)
				return
			}

			contexts[profileID] = nil
			application.deleteContext(context) { [weak self] deleted in
				self?.pendingDeletions.remove(profileID)

				if deleted {
					self?.deletions.remove(profileID)
				}
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

	// Never behind the lock: no page view sits under the PIN screen, and the
	// window's title (the Window menu's, Mission Control's) stays Pane.
	private func show(_ page: WebContents, profileID: String) {
		guard interactionAllowed else {
			return
		}

		if let context = contexts[profileID] {
			browserWindow.activeContext = context
		}

		browserWindow.activateTab(page)
		placeActivePage()
		dialogs.selectionChanged()
		// Actions show their state for the selected tab.
		refreshExtensions(profileID)
		browserWindow.window.title = page.title.isEmpty ? "Pane" : page.title
	}

	private func tabID(of page: WebContents) -> String? {
		tabIDs[ObjectIdentifier(page)]
	}

	// The selected tab's page (and its docked DevTools) in the panel on the
	// browser page. Under Settings and Extensions, which cover it, it leaves
	// the panel and is hidden, as a background tab is, so it stops drawing:
	// hidden in place, the engine kept the window's active tab drawing.
	// Never while locked: the page shows once the lock opens.
	private func placeActivePage() {
		guard interactionAllowed, let tabID = model.activeTabID, let page = pages[tabID] else {
			return
		}

		let docked = devTools[tabID]

		CATransaction.begin()
		CATransaction.setDisableActions(true)
		defer { CATransaction.commit() }

		if model.page == .browser {
			pageHost.show(page, devTools: docked)
			page.isVisible = true
			docked?.isVisible = true
		} else {
			pageHost.show(nil)
			page.isVisible = false
			docked?.isVisible = false
		}
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
		removeTabAndReselect(tabID)
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
			placeActivePage()
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
			guard let context else {
				self?.model.setCrashed(tab.id, true)
				return
			}

			self?.openPage(tabID: tab.id, url: url, context: context, foreground: false)
		}
	}

	func toggleDevTools() {
		guard interactionAllowed, let page = activePage else {
			return
		}

		page.isDevToolsOpen ? page.closeDevTools() : page.showDevTools()
	}

	func printPage() {
		guard interactionAllowed else {
			return
		}

		activePage?.printPage()
	}

	// The sad tab's Reload, or any reload of a crashed tab. A tab whose
	// profile would not open has no page: selecting it tries again.
	func reloadCrashed(_ tabID: String) {
		model.setCrashed(tabID, false)
		lastCrash[tabID] = nil

		if let page = pages[tabID] {
			page.reload()
		} else if model.activeTabID == tabID {
			select(tabID: tabID)
		}
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
		// In the window, as Aside's popup: an AppKit sheet takes some 280 ms
		// to attach, with the main thread held.
		presentExtensionPrompt(WindowState.ExtensionPrompt(
			kind: .install,
			extensionID: request.extensionIdentifier,
			title: Self.installTitle(request),
			icon: request.icon,
			permissionsHeading: request.permissionsHeading,
			permissions: request.permissions,
			acceptTitle: request.acceptTitle == "Add extension" ? "Add to Pane" : request.acceptTitle,
			cancelTitle: request.cancelTitle,
			answer: { accepted, _ in accepted ? request.accept() : request.cancel() }
		))
	}

	func browserContext(_ context: BrowserContext, didInstall installed: Extension) {
		refreshExtensions(context.identifier)
	}

	func browserContext(_ context: BrowserContext, show popup: ExtensionPopup, in window: BrowserWindow) {
		guard interactionAllowed, let hostingView else {
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
			guard let self, let context else {
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

	// Adding names Pane, as Dia's dialog names Dia; Chrome's other prompts
	// (new permissions, re-enabling) keep their titles.
	static func installTitle(_ request: ExtensionInstallRequest) -> String {
		request.acceptTitle == "Add extension"
			? "Add \u{201C}\(request.extensionName)\u{201D} to Pane?"
			: curlyQuoted(request.title)
	}

	// Chrome's prompt titles quote the name straight (Add "Dark Reader"?);
	// the popups quote it as Remove does.
	static func curlyQuoted(_ title: String) -> String {
		guard let open = title.firstIndex(of: "\""), let close = title.lastIndex(of: "\""), open < close else {
			return title
		}

		return title.replacingCharacters(in: close ... close, with: "\u{201D}").replacingCharacters(in: open ... open, with: "\u{201C}")
	}

	// Removes after asking, in the popup, as Aside does; Report abuse then
	// opens the Web Store's report page, as Chrome's dialog does.
	func confirmUninstall(_ item: ExtensionInfo, inProfile profileID: String) {
		presentExtensionPrompt(WindowState.ExtensionPrompt(
			kind: .remove,
			extensionID: item.id,
			title: "Remove \u{201C}\(item.name)\u{201D}?",
			icon: item.icon,
			permissionsHeading: "",
			permissions: [],
			acceptTitle: "Remove",
			cancelTitle: "Cancel",
			answer: { [weak self] accepted, reportAbuse in
				guard accepted, let self else {
					return
				}

				uninstallExtension(item.id, inProfile: profileID)

				if reportAbuse, let url = URL(string: "https://chrome.google.com/webstore/report/\(item.id)?utm_source=chrome-remove-extension-dialog") {
					openTab(inProfile: profileID, url: url)
				}
			}
		))
	}
}

// MARK: - Extensions menu and popups

// The toolbar puzzle's menu (ExtensionsMenu), an extension's more menu
// beside it, and the popups that add or remove one (ExtensionPromptView), as
// Aside's. Esc closes the topmost; a menu also closes on a click outside it,
// or once one of its items runs.
extension BrowserController {
	static let webStoreURL = URL(string: "https://chromewebstore.google.com/category/extensions")!

	func toggleExtensionsMenu() {
		if state.extensionsMenuOpen {
			closeExtensionsMenu()
			return
		}

		guard model.activeProfileID != nil else {
			return
		}

		state.extensionsMenuOpen = true
		syncExtensionsKeyMonitor()
	}

	// The menu, and the more menu beside it.
	func closeExtensionsMenu() {
		state.extensionMoreMenu = nil
		state.extensionsMenuOpen = false
		syncExtensionsKeyMonitor()
	}

	func toggleExtensionMoreMenu(_ extensionID: String, row: CGRect) {
		state.extensionMoreMenu = state.extensionMoreMenu?.extensionID == extensionID ? nil : (extensionID, row)
	}

	// The Extensions page, at one extension's card if given.
	func showExtensions(extensionID: String? = nil) {
		closeExtensionsMenu()
		state.extensionsAnchor = extensionID.map { "pane.extensions.extension.\($0)" }
		model.page = .extensions
	}

	// The Chrome Web Store in a new tab of the profile (the active one by
	// default): its Add to Chrome installs into that profile.
	func visitWebStore(inProfile profileID: String? = nil) {
		closeExtensionsMenu()

		if let profileID = profileID ?? model.activeProfileID {
			openTab(inProfile: profileID, url: Self.webStoreURL)
		}
	}

	// An extension's page in the Web Store, where it came from.
	func openExtensionStorePage(_ extensionID: String) {
		closeExtensionsMenu()

		if let profileID = model.activeProfileID, let url = URL(string: "https://chromewebstore.google.com/detail/\(extensionID)") {
			openTab(inProfile: profileID, url: url)
		}
	}

	func openExtensionOptions(_ item: ExtensionInfo, inProfile profileID: String? = nil) {
		closeExtensionsMenu()

		if let profileID = profileID ?? model.activeProfileID, let url = item.optionsURL {
			openTab(inProfile: profileID, url: url)
		}
	}

	// A row of the menu: the extension's action, its popup at the puzzle when
	// it is not pinned.
	func runExtensionFromMenu(_ extensionID: String) {
		closeExtensionsMenu()
		runExtensionAction(extensionID)
	}

	// A popup replaces one still open, which answers no. Behind the lock the
	// answer is no at once.
	func presentExtensionPrompt(_ prompt: WindowState.ExtensionPrompt) {
		guard interactionAllowed else {
			prompt.answer(false, false)
			return
		}

		closeExtensionsMenu()
		let previous = state.extensionPrompt
		state.extensionPromptReportsAbuse = false
		state.extensionPrompt = prompt
		syncExtensionsKeyMonitor()
		previous?.answer(false, false)
	}

	func answerExtensionPrompt(accepted: Bool) {
		guard let prompt = state.extensionPrompt else {
			return
		}

		state.extensionPrompt = nil
		syncExtensionsKeyMonitor()
		prompt.answer(accepted, accepted && state.extensionPromptReportsAbuse)
	}

	// The keys belong to whichever of them shows: a popup takes all but
	// shortcuts, as a dialog would.
	private func syncExtensionsKeyMonitor() {
		let showing = state.extensionsMenuOpen || state.extensionPrompt != nil

		if showing, extensionsKeyMonitor == nil {
			extensionsKeyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
				// Event monitors run on the main thread.
				nonisolated(unsafe) let event = event
				let consumed = MainActor.assumeIsolated { self?.extensionsUIConsumes(event) ?? false }

				return consumed ? nil : event
			}
		} else if !showing, let extensionsKeyMonitor {
			NSEvent.removeMonitor(extensionsKeyMonitor)
			self.extensionsKeyMonitor = nil
		}
	}

	private func extensionsUIConsumes(_ event: NSEvent) -> Bool {
		let isPrompt = state.extensionPrompt != nil

		switch event.keyCode {
		case 53:
			if isPrompt {
				answerExtensionPrompt(accepted: false)
			} else if state.extensionMoreMenu != nil {
				state.extensionMoreMenu = nil
			} else {
				closeExtensionsMenu()
			}

			return true

		case 36, 76:
			guard isPrompt else {
				return false
			}

			answerExtensionPrompt(accepted: true)
			return true

		default:
			return isPrompt && event.modifierFlags.intersection(.deviceIndependentFlagsMask).isDisjoint(with: [.command, .control])
		}
	}
}

extension Notification.Name {
	static let paneDownloadDidUpdate = Notification.Name("PaneDownloadDidUpdate")
}

extension NSImage {
	// The most pixels a favicon keeps a side: it draws at 16 points, 2x.
	static let faviconPixels = 32

	// A PNG data: URL, the form tabs persist favicons in, at most
	// faviconPixels a side. A site's 180-pixel touch icon would otherwise
	// weigh tens of KB of base64 in profiles.json, written at every save.
	var pngDataURL: String? {
		guard let image = cgImage(forProposedRect: nil, context: nil, hints: nil), image.width > 0, image.height > 0 else {
			return nil
		}

		let scale = min(1, Double(Self.faviconPixels) / Double(max(image.width, image.height)))
		let width = max(1, Int((Double(image.width) * scale).rounded()))
		let height = max(1, Int((Double(image.height) * scale).rounded()))

		guard
			let bitmap = NSBitmapImageRep(
				bitmapDataPlanes: nil, pixelsWide: width, pixelsHigh: height, bitsPerSample: 8,
				samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
				bytesPerRow: 0, bitsPerPixel: 0
			),
			let context = NSGraphicsContext(bitmapImageRep: bitmap)
		else {
			return nil
		}

		context.imageInterpolation = .high
		context.cgContext.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))

		guard let png = bitmap.representation(using: .png, properties: [:]) else {
			return nil
		}

		return "data:image/png;base64,\(png.base64EncodedString())"
	}
}

// MARK: - Sidebar width

extension BrowserController {
	// Each step of a drag of the sidebar's edge: the window lays out at the
	// new width, and settings.json waits for the drag to end.
	func resizeSidebar(to width: CGFloat) {
		state.sidebarWidth = CGFloat(SidebarWidth.clamped(Double(width)))
	}

	func endSidebarResize() {
		model.settings.sidebarWidth = Double(state.sidebarWidth)
	}

	// A double-click on the edge.
	func resetSidebarWidth() {
		state.sidebarWidth = CGFloat(SidebarWidth.standard)
		endSidebarResize()
	}
}
