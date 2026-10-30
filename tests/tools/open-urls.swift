// Hands URLs to a running Pane as another app does: through Launch Services,
// which sends them to the app as Apple Events (the way a link clicked in Mail
// reaches the default browser), without activating it. Only the copy running
// as <pid> from <app> may get them: Launch Services would start another copy
// for a bundle that is not running, so the tool refuses then, and a copy it
// started anyway gets a throwaway <home> and is stopped. Prints, as JSON, the
// pid that got them.
// Usage: open-urls <app> <pid> <home> <url>...
import AppKit

let arguments = CommandLine.arguments

guard arguments.count >= 5, let pid = Int32(arguments[2]) else {
	FileHandle.standardError.write(Data("usage: open-urls <app> <pid> <home> <url>...\n".utf8))
	exit(2)
}

func fail(_ message: String) -> Never {
	FileHandle.standardError.write(Data("open-urls: \(message)\n".utf8))
	exit(1)
}

let app = URL(fileURLWithPath: arguments[1]).resolvingSymlinksInPath()
let home = arguments[3]
let urls = arguments.dropFirst(4).compactMap(URL.init(string:))

guard let running = NSRunningApplication(processIdentifier: pid),
      running.bundleURL?.resolvingSymlinksInPath() == app
else {
	fail("no copy of \(app.path) runs as \(pid)")
}

let configuration = NSWorkspace.OpenConfiguration()
configuration.activates = false
configuration.createsNewApplicationInstance = false
configuration.environment = ["HOME": home, "CFFIXED_USER_HOME": home]

let done = DispatchSemaphore(value: 0)
var receiver: NSRunningApplication?
var failure: Error?

NSWorkspace.shared.open(urls, withApplicationAt: app, configuration: configuration) { application, error in
	receiver = application
	failure = error
	done.signal()
}

if done.wait(timeout: .now() + 30) == .timedOut {
	fail("Launch Services did not answer")
}

if let failure {
	fail("\(failure)")
}

if let receiver, receiver.processIdentifier != pid {
	receiver.forceTerminate()
	fail("Launch Services started another copy (\(receiver.processIdentifier)), now stopped")
}

print(#"{"pid":\#(receiver?.processIdentifier ?? 0)}"#)
