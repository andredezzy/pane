import AppKit
import PaneCore
import PaneKit

// Receives the engine's lifecycle and owns the app's model, window, and menu.
@MainActor
final class AppDelegate: NSObject, BrowserApplicationDelegate {
	let dataDirectory: URL
	private var controller: BrowserController?
	private var store: ModelStore?
	private var vault: DataVault?
	private var windowController: MainWindowController?
	private var keys: KeyRouter?
	private var automation: Automation?
	private var sleep: SleepController?
	private var observers: [NSObjectProtocol] = []

	// The data's lock and the image's attach, from before the engine
	// (main.swift): neither when another Pane held the data then.
	private let dataLock: DataLock?
	private let image: Task<DataVolume.Devices?, Never>?

	init(dataDirectory: URL, dataLock: DataLock?, image: Task<DataVolume.Devices?, Never>?) {
		self.dataDirectory = dataDirectory
		self.dataLock = dataLock
		self.image = image
	}

	func browserApplicationDidStart(_ application: BrowserApplication) {
		// Before the controller has the engine make the window.
		TitleBar.prepare()

		// The data key opens the profiles (ADR 0113): the model starts with
		// its settings alone, and the lock, or the launch without a PIN, opens
		// the rest.
		let vault = DataVault(dataDirectory: dataDirectory)
		let lock = AppLock(dataDirectory: dataDirectory, vault: vault)
		let store = ModelStore(dataDirectory: dataDirectory, vault: vault)
		let model = store.makeModel()
		let updater = Updater(currentVersion: Bundle.main.object(forInfoDictionaryKey: "PaneVersion") as? String ?? "0.0.0")
		let controller = BrowserController(
			model: model,
			dataDirectory: dataDirectory,
			application: application,
			lock: lock,
			vault: vault,
			updater: updater
		)
		updater.schedule()
		self.controller = controller
		self.store = store
		self.vault = vault

		vault.profileIDs = { model.profiles.map(\.id) }
		vault.didOpen = { [weak store, weak controller] in
			store?.loadProfiles()
			controller?.profilesDidLoad()
		}
		vault.didBecomeOpen = { [weak controller] in
			controller?.accessDidOpen()
		}
		vault.didFail = { [weak lock] in
			lock?.volumeWontOpen()
		}

		// An earlier Pane's profiles move in at once, with no PIN to ask
		// first: the panel shows from the first frame.
		if !lock.isLocked, vault.volume.hasPlainProfiles {
			vault.expectMove()
		}

		controller.applyTheme()
		controller.dockIcon.show(model.settings.appIcon)
		NSApp.mainMenu = MainMenu.build(target: controller)
		windowController = MainWindowController(controller: controller)
		keys = KeyRouter(controller: controller)
		sleep = SleepController(controller: controller)

		if let directory = ProcessInfo.processInfo.environment["PANE_AUTOMATION_DIR"] {
			let automation = Automation(directory: URL(fileURLWithPath: directory), controller: controller, store: store)
			controller.probe = automation
			self.automation = automation
		}

		observers.append(NotificationCenter.default.addObserver(
			forName: BrowserApplication.shouldReopenNotification,
			object: nil,
			queue: .main
		) { [weak self] _ in
			MainActor.assumeIsolated { self?.windowController?.showWindow() }
		})

		windowController?.showWindow()
		vault.adopt(image, lock: dataLock)
		lock.start()

		if let url = CommandLine.arguments.dropFirst().first(where: { !$0.hasPrefix("-") }).flatMap(URL.init(string:)) {
			controller.openLaunchURLs([url])
		}
	}

	// Another app's URLs open in the window, shown again if it was closed:
	// web pages and files, nothing else that reaches the engine.
	func browserApplication(_ application: BrowserApplication, open urls: [URL]) {
		let pages = urls.filter { ["http", "https", "file"].contains($0.scheme?.lowercased()) }

		guard !pages.isEmpty else {
			return
		}

		controller?.openLaunchURLs(pages)
		windowController?.showWindow()
	}

	func browserApplicationWillShutDown(_ application: BrowserApplication) {
		store?.saveNow()
		// Saved first: a staged update swaps in now (ADR 0092).
		controller?.updater.installAtQuit()
		// AppKit keeps the open panel's last folder in Pane's preferences:
		// a trace of the user's files outside the volume (ADR 0113).
		UserDefaults.standard.removeObject(forKey: "NSOSPLastRootDirectory")
	}

	// After the engine has shut down, when nothing has the volume's files
	// open (main.swift).
	func engineDidStop() {
		vault?.close()
	}
}
