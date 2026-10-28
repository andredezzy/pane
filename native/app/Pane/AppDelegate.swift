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
	private var sleep: SleepController?
	private var observers: [NSObjectProtocol] = []

	init(dataDirectory: URL) {
		self.dataDirectory = dataDirectory
	}

	func browserApplicationDidStart(_ application: BrowserApplication) {
		let model = BrowserModel(dataDirectory: dataDirectory)
		let updater = Updater(currentVersion: Bundle.main.object(forInfoDictionaryKey: "PaneVersion") as? String ?? "0.0.0")
		let controller = BrowserController(
			model: model,
			application: application,
			lock: AppLock(dataDirectory: dataDirectory),
			updater: updater
		)
		updater.schedule()
		self.controller = controller

		controller.applyTheme()
		NSApp.mainMenu = MainMenu.build(target: controller)
		windowController = MainWindowController(controller: controller)
		switcherKeys = SwitcherKeys(controller: controller)
		sleep = SleepController(controller: controller)

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

		if let url = CommandLine.arguments.dropFirst().first(where: { !$0.hasPrefix("-") }).flatMap(URL.init(string:)) {
			controller.openLaunchURL(url)
		}
	}

	func browserApplicationWillShutDown(_ application: BrowserApplication) {
		controller?.model.saveNow()
	}
}
