import AppKit
import PaneCore

// Chromium owns the process from here: `run` starts the engine on the main
// thread and returns only at shutdown. Nothing may touch NSApplication before
// it, because the engine creates the shared application itself.
let dataDirectory = ProcessInfo.processInfo.environment["PANE_DATA_DIR"].map { URL(fileURLWithPath: $0, isDirectory: true) }
	?? FileManager.default
	.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
	.appendingPathComponent("com.pane.desktop", isDirectory: true)

// Pane restores its own session (profiles.json), so AppKit's window
// restoration stays off, along with its "reopen windows?" alert after a crash.
UserDefaults.standard.register(defaults: [
	"ApplePersistenceIgnoreState": true,
	// The app's text as the Electron app draws its UI (CSS
	// -webkit-font-smoothing: antialiased): no font smoothing.
	"AppleFontSmoothing": 0,
	"NSQuitAlwaysKeepsWindows": false,
])

// Started again by the panic wipe: once the wiped instance has exited, the
// data is erased once more, in case the engine wrote something on its way out.
if let argument = CommandLine.arguments.first(where: { $0.hasPrefix(AppLock.wipeSwitch) }),
   let wiped = pid_t(argument.dropFirst(AppLock.wipeSwitch.count))
{
	let deadline = Date().addingTimeInterval(10)

	while kill(wiped, 0) == 0, Date() < deadline {
		usleep(100_000)
	}

	AppLock.erase(dataDirectory)
}

let appDelegate = AppDelegate(dataDirectory: dataDirectory)

exit(BrowserApplication.run(delegate: appDelegate, dataDirectory: dataDirectory))
