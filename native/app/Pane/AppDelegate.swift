import AppKit
import PaneCore
import PaneKit

// Receives the engine's lifecycle and owns the app's model, window, and menu.
@MainActor
final class AppDelegate: NSObject, BrowserApplicationDelegate {
	let dataDirectory: URL
	private var controller: BrowserController?
	private var windowController: MainWindowController?
	private var switcherKeys: SwitcherKeys?
	private var automation: Automation?
	private var observers: [NSObjectProtocol] = []

	init(dataDirectory: URL) {
		self.dataDirectory = dataDirectory
	}

	func browserApplicationDidStart(_ application: BrowserApplication) {
		let model = BrowserModel(dataDirectory: dataDirectory)
		let controller = BrowserController(model: model, application: application)
		self.controller = controller

		controller.applyTheme()
		NSApp.mainMenu = MainMenu.build(target: controller)
		windowController = MainWindowController(controller: controller)
		switcherKeys = SwitcherKeys(controller: controller)

		if let directory = ProcessInfo.processInfo.environment["PANE_AUTOMATION_DIR"] {
			automation = Automation(directory: URL(fileURLWithPath: directory), controller: controller)
		}

		observers.append(NotificationCenter.default.addObserver(
			forName: BrowserApplication.shouldReopenNotification,
			object: nil,
			queue: .main
		) { [weak self] _ in
			MainActor.assumeIsolated { self?.windowController?.showWindow() }
		})

		windowController?.showWindow()
		openLaunchURL(controller)
	}

	func browserApplicationWillShutDown(_ application: BrowserApplication) {
		controller?.model.saveNow()
	}

	// `Pane https://…` opens the URL as a new tab, in the first profile (made
	// on first launch when there is none).
	private func openLaunchURL(_ controller: BrowserController) {
		guard let url = CommandLine.arguments.dropFirst().first(where: { !$0.hasPrefix("-") }).flatMap(URL.init(string:)) else {
			return
		}

		let profileID = controller.model.profiles.first?.id
			?? controller.model.createProfile(name: "Personal", color: .blue, group: nil)
		controller.model.expandedProfileIDs.insert(profileID)
		controller.openTab(inProfile: profileID, url: url)
	}
}
