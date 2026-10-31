import AppKit
import PaneCore

// What scenarios watch that the app itself never reads: a page printing, the
// page context menu, downloads, a profile's engine data going. The
// controller reports each to its probe, which is nil unless
// PANE_AUTOMATION_DIR is set in a scenario's run (Automation, TestHooks),
// and held weakly, so a shipped
// Pane keeps none of this state.
@MainActor
protocol AutomationProbe: AnyObject {
	func pageWillPrint()
	// A page's right-click menu, built, then on screen until it closes.
	func contextMenu(_ menu: NSMenu, isOpen: Bool)
	func downloadDidUpdate(_ download: Download)
	// A deleted profile's engine data, still going, or gone (or kept: its
	// context would not open).
	func profileDeletion(_ profileID: String, isPending: Bool)
}
