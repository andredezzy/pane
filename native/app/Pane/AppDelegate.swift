import AppKit
import PaneCore

// Receives the engine's lifecycle and owns the app's windows and menu bar.
@MainActor
final class AppDelegate: NSObject, BrowserApplicationDelegate {
	private var windowController: MainWindowController?
	private var reopenObserver: NSObjectProtocol?

	func browserApplicationDidStart(_ application: BrowserApplication) {
		NSApp.mainMenu = MainMenu.build()

		reopenObserver = NotificationCenter.default.addObserver(
			forName: BrowserApplication.shouldReopenNotification,
			object: nil,
			queue: .main
		) { [weak self] _ in
			MainActor.assumeIsolated {
				self?.windowController?.showWindow()
			}
		}

		application.openContext(identifier: "main") { [weak self] context, error in
			guard let self, let context else {
				NSLog("Pane: could not open the main profile: %@", String(describing: error))
				return
			}

			let controller = MainWindowController(context: context)
			self.windowController = controller
			controller.showWindow()
			controller.load(Self.initialURL())
		}
	}

	// The first plain argument that parses as a URL, so `Pane https://…` opens
	// it, like any browser.
	private static func initialURL() -> URL {
		let candidate = CommandLine.arguments.dropFirst().first { !$0.hasPrefix("-") }

		return candidate.flatMap(URL.init(string:)) ?? URL(string: "about:blank")!
	}
}
