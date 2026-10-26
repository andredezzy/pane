// Turns on an app's full accessibility as VoiceOver does when it starts: it
// asks the app for its role and sets AXEnhancedUserInterface. Chrome then
// builds its pages' accessibility trees with their actions (a web button
// presses only in that mode). cua-driver reads windows without doing either,
// so web content stays out of its reads until something does.
// Usage: ax-activate <pid>
import ApplicationServices
import Foundation

guard CommandLine.arguments.count == 2, let pid = pid_t(CommandLine.arguments[1]) else {
	print("usage: ax-activate <pid>")
	exit(1)
}

let app = AXUIElementCreateApplication(pid)
var role: AnyObject?

// Chrome acts on the value, then AppKit reports the attribute as not
// settable, so the result of setting it says nothing. Chrome waits for two
// quiet seconds before it turns full accessibility on; the request lapses if
// its client is gone by then.
AXUIElementCopyAttributeValue(app, kAXRoleAttribute as CFString, &role)
AXUIElementSetAttributeValue(app, "AXEnhancedUserInterface" as CFString, kCFBooleanTrue)
Thread.sleep(forTimeInterval: 3)

print(role as? String ?? "no role")
