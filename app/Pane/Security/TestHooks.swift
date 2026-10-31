import Foundation

// What the scenarios set (tests/lib): a run on the mock keychain, whose
// fixed secret opens none of the user's data, sealed under the Keychain's
// (ADR 0113). The scenarios' hooks work there alone: the automation channel,
// a data root or update server of their own, the sleep clock's scale. A
// process of the user's that starts Pane's own binary with them gets nothing
// from them, where the channel had read every tab of a launch without a PIN.
enum TestHooks {
	static let enabled = CommandLine.arguments.contains("--use-mock-keychain")

	// A hook's variable, in a scenario's run only.
	static func environment(_ name: String) -> String? {
		enabled ? ProcessInfo.processInfo.environment[name] : nil
	}
}
