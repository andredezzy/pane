import AppKit
import PaneCore
import PaneKit
import os

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
	// Each loaded tab's page, and the DevTools docked to it.
	private let pages = PageRegistry()
	// When each tab's renderer last died, to tell a crash loop from a crash.
	private var crashes = CrashLoop()
	// Built on first use: it needs the window.
	private(set) lazy var dialogs = DialogPresenter(
		window: browserWindow.window,
		activeTabID: { [weak self] in self?.model.activeTabID },
		isHeld: { [weak self] in self?.interactionAllowed == false }
	)
	// Scenarios' view of what the app never reads; nil in a shipped Pane.
	weak var probe: AutomationProbe?
	// The extensions feature: lists, the toolbar's menu, popups.
	private(set) lazy var extensions = ExtensionsController(browser: self)
	// The window's full screen, with a page's fullscreen.
	private lazy var fullScreen = FullScreenFollower(controller: self)
	// The window's SwiftUI root, where toolbar buttons anchor popups.
	weak var hostingView: NSView?
	// The address bar's load progress, as Chrome's toolbar shows the
	// selected tab's.
	let progressBar = ToolbarProgress()
	// The App icon setting's window in the Dock (ADR 0089).
	let dockIcon = DockIcon()
	// Profiles whose engine data is still being deleted, on disk until the
	// engine confirms, for the next launch.
	private let deletions: ProfileDeletions
	// Each profile's proxy and WebRTC policy.
	let network: ProfileNetwork
	// The PIN lock.
	let lock: AppLock
	// The update check and download.
	let updater: Updater
	// The URLs Pane was started with or handed, while they wait behind the
	// lock.
	private var launchURLs: [URL] = []
	// A new tab with no page of the user's: its address field takes the keys
	// once it shows, to type in, rather than its page.
	private var focusesAddress: String?

	init(model: BrowserModel, dataDirectory: URL, application: BrowserApplication, lock: AppLock, updater: Updater) {
		self.model = model
		self.application = application
		self.lock = lock
		self.updater = updater
		network = ProfileNetwork(model: model)
		deletions = ProfileDeletions(dataDirectory: dataDirectory)
		browserWindow = BrowserWindow(contentRect: NSRect(x: 0, y: 0, width: 1280, height: 800))
		super.init()
		browserWindow.delegate = self
		state.sidebarWidth = CGFloat(model.settings.sidebarWidth)
		lock.didUnlock = { [weak self] in
			self?.lockDidOpen()
		}

		// The sidebar shows each proxy's state from launch, as the Electron
		// app tested them.
		for profile in model.profiles where profile.proxy != nil {
			network.test(profile.id)
		}
	}

	func proxyDidChange(_ profileID: String) {
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
	func fingerprintDidChange(_ profileID: String) {
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
			Log.engine.error("Could not apply the fingerprint of profile \(profileID): \(String(describing: error), privacy: .private)")
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
		if !launchURLs.isEmpty {
			let urls = launchURLs
			launchURLs = []
			openLaunchURLs(urls)
		}

		if let page = activePage, let profileID = model.activeProfileID {
			show(page, profileID: profileID)
		}

		dialogs.resume()
	}

	// What Pane is started with (`Pane https://…`) or another app hands it (a
	// link clicked in Mail, a file opened with Pane) opens as new tabs, in
	// order, in the profile in use, as Chrome opens them in the last one used
	// (the first profile, made on first launch when there is none), once the
	// lock is open. The last one is selected.
	func openLaunchURLs(_ urls: [URL]) {
		guard interactionAllowed else {
			launchURLs += urls
			return
		}

		guard !urls.isEmpty else {
			return
		}

		let profileID = model.activeProfileID ?? model.profiles.first?.id
			?? model.createProfile(name: "Personal", color: .blue, group: nil)
		expandProfile(profileID)

		for url in urls {
			openTab(inProfile: profileID, url: url)
		}
	}

	// MARK: Contexts

	// Runs `body` with the profile's context, opening it first if needed;
	// with nil when it would not open, so no caller is left half done.
	func withContext(_ profileID: String, _ body: @escaping (BrowserContext?) -> Void) {
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
				Log.engine.error("Could not open profile \(profileID): \(String(describing: error), privacy: .private)")
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
				extensions.refresh(profileID)
				waiters.forEach { $0(context) }
			}
		}
	}

	func context(for profileID: String) -> BrowserContext? {
		contexts[profileID]
	}

	// MARK: Tabs

	var activePage: WebContents? {
		model.activeTabID.flatMap(pages.page)
	}

	func page(for tabID: String) -> WebContents? {
		pages.page(for: tabID)
	}

	// Selects a tab, loading its page if it has none.
	func select(tabID: String) {
		guard interactionAllowed, let tab = model.tab(tabID), let profileID = model.profileID(ofTab: tabID) else {
			return
		}

		Log.interaction("Select tab")

		let previous = model.activeTabID
		model.select(tabID: tabID)
		model.setPage(.browser)
		model.setExpanded(profileID, true)

		if previous != tabID {
			progressBar.tabChanged(progress: model.loadingProgress[tabID], at: CACurrentMediaTime())
		}

		if previous != tabID, let previous {
			hideAfterFrame(previous)
		}

		if let page = pages.page(for: tabID) {
			show(page, profileID: profileID)
			return
		}

		withContext(profileID) { [weak self] context in
			guard let self, model.activeTabID == tabID, pages.page(for: tabID) == nil else {
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

	// Hides the tab left, once the frame showing the tab entered is drawn:
	// Chromium's WasHidden and the view's hiding took 4.5 of a tab switch's
	// 6.5 ms, ahead of the frame. The tab entered has replaced its view in the
	// window already; one selected again meanwhile stays shown.
	private func hideAfterFrame(_ tabID: String) {
		DispatchQueue.main.async { [weak self] in
			guard let self, model.activeTabID != tabID, let page = pages.page(for: tabID) else {
				return
			}

			page.isVisible = false
			pages.devTools(for: tabID)?.isVisible = false
		}
	}

	// A new tab in `profileID`, selected. Without a URL it opens the start page
	// and the address field takes focus.
	@discardableResult
	func openTab(inProfile profileID: String, url: URL? = nil) -> String {
		let tab = Tab(id: UUID().uuidString.lowercased(), url: (url ?? AddressInput.newTabURL).absoluteString)
		model.insertTab(tab, inProfile: profileID, after: nil)

		if url == nil {
			focusesAddress = tab.id
		}

		select(tabID: tab.id)
		return tab.id
	}

	func newTabInActiveProfile() {
		Log.interaction("New tab")
		guard let profileID = model.activeProfileID ?? model.profiles.first?.id else {
			return
		}

		openTab(inProfile: profileID)
	}

	// A loaded tab goes once its page agrees to close (beforeunload may keep
	// it); the engine's didRemoveTab then takes it out. An unloaded tab goes
	// at once.
	func closeTab(_ tabID: String) {
		if let page = pages.page(for: tabID) {
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
		crashes.forget(tabID)
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
		guard let page = pages.remove(tabID: tabID) else {
			return
		}

		page.nativeView.removeFromSuperview()
		page.closeImmediately()
		crashes.forget(tabID)
		model.setLoaded(tabID, false)

		if model.activeTabID == tabID {
			progressBar.tabChanged(progress: nil, at: CACurrentMediaTime())
		}
	}

	// Removes the profile now; its engine data goes once the engine confirms.
	// A profile not opened this session is opened first, so data from earlier
	// sessions goes too. Until then deletions.json names it, so a quit, a
	// crash, or a context that would not open leaves the next launch to
	// remove its folder (main.swift).
	func deleteProfile(_ profileID: String) {
		unloadProfile(profileID)
		model.removeProfile(profileID)
		extensions.forget(profileID)
		probe?.profileDeletion(profileID, isPending: true)
		deletions.add(profileID)
		network.stop(profileID)

		withContext(profileID) { [weak self] context in
			guard let self else {
				return
			}

			guard let context else {
				probe?.profileDeletion(profileID, isPending: false)
				return
			}

			contexts[profileID] = nil
			application.deleteContext(context) { [weak self] deleted in
				self?.probe?.profileDeletion(profileID, isPending: false)

				if deleted {
					self?.deletions.remove(profileID)
				}
			}
		}
	}

	// MARK: Pages

	private func openPage(tabID: String, url: URL?, context: BrowserContext, foreground: Bool) {
		browserWindow.activeContext = context
		let page = pages.opening(tabID) {
			browserWindow.openTab(url: url, context: context, foreground: foreground)
		}

		if let page, pages.page(for: tabID) == nil {
			adopt(page, tabID: tabID)
		}

		if model.activeTabID == tabID, let page = pages.page(for: tabID) {
			show(page, profileID: context.identifier)
		}
	}

	private func adopt(_ page: WebContents, tabID: String) {
		pages.adopt(page, tabID: tabID)
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
		extensions.refresh(profileID)
		browserWindow.window.title = page.title.isEmpty ? "Pane" : page.title
		focusEnteredTab(page)
	}

	// The tab entered takes the keys, as Chrome's tab switch gives them to its
	// page: as the last page's view left the window with them, AppKit gave
	// them to the address field, which selected its text (the user's report).
	// A new tab's address field takes them instead, to type in.
	private func focusEnteredTab(_ page: WebContents) {
		guard let tabID = tabID(of: page) else {
			return
		}

		if focusesAddress == tabID {
			focusesAddress = nil
			state.focusAddressField()
		} else {
			page.focus()
		}
	}

	private func tabID(of page: WebContents) -> String? {
		pages.tabID(of: page)
	}

	// The selected tab's page (and its docked DevTools) in the panel on the
	// browser page. Under Settings and Extensions, which cover it, it leaves
	// the panel and is hidden, as a background tab is, so it stops drawing:
	// hidden in place, the engine kept the window's active tab drawing.
	// Never while locked: the page shows once the lock opens.
	func placeActivePage() {
		guard interactionAllowed, let tabID = model.activeTabID, let page = pages.page(for: tabID) else {
			return
		}

		let docked = pages.devTools(for: tabID)

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
		if let tabID = pages.pendingTabID {
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
		pages.remove(tabID: tabID)
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
				pages.page(for: tabID) != nil
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
		fullScreen.follow()
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

		let previous = model.loadingProgress[tabID]
		let current = isLoading ? progress : nil
		model.setLoading(tabID, progress: current)

		if model.activeTabID == tabID {
			progressBar.reducesMotion = Motion.isReduced
			progressBar.loadingChanged(from: previous, to: current, at: CACurrentMediaTime())
		}
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

		if model.activeTabID == tabID {
			progressBar.crashed(at: CACurrentMediaTime())
		}

		// The first crash reloads the page, as a slept tab wakes. A page that
		// dies again within a minute shows the sad tab instead of looping.
		if crashes.crashed(tabID, at: Date()) {
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
		probe?.contextMenu(menu, isOpen: false)
		let view = webContents.nativeView
		let point = NSPoint(
			x: context.location.x,
			y: view.isFlipped ? context.location.y : view.bounds.height - context.location.y
		)

		// Shown once the engine's task returns: the menu runs its own loop.
		DispatchQueue.main.async { [weak self] in
			self?.probe?.contextMenu(menu, isOpen: true)
			menu.popUp(positioning: nil, at: point, in: view)
			self?.probe?.contextMenu(menu, isOpen: false)
		}
	}

	func webContentsWillPrint(_ webContents: WebContents) {
		probe?.pageWillPrint()
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

		pages.setDevTools(tools, for: tabID)?.isVisible = false
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
		crashes.forget(tabID)

		if let page = pages.page(for: tabID) {
			page.reload()
		} else if model.activeTabID == tabID {
			select(tabID: tabID)
		}
	}
}

// MARK: - BrowserContextDelegate

extension BrowserController: BrowserContextDelegate {
	func browserContext(_ context: BrowserContext, didUpdate download: Download) {
		probe?.downloadDidUpdate(download)
	}

	func browserContextDidChangeExtensions(_ context: BrowserContext) {
		extensions.refresh(context.identifier)
	}

	func browserContext(_ context: BrowserContext, confirmExtensionInstall request: ExtensionInstallRequest) {
		extensions.confirmInstall(request)
	}

	func browserContext(_ context: BrowserContext, didInstall installed: Extension) {
		extensions.refresh(context.identifier)
	}

	func browserContext(_ context: BrowserContext, show popup: ExtensionPopup, in window: BrowserWindow) {
		extensions.show(popup)
	}
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
		changeSettings { $0.sidebarWidth = Double(state.sidebarWidth) }
	}

	// A double-click on the edge.
	func resetSidebarWidth() {
		state.sidebarWidth = CGFloat(SidebarWidth.standard)
		endSidebarResize()
	}
}
