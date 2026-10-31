import AppKit
import PaneKit

// When Pane locks by itself (ADR 0116, Paper 06.4): when the Mac sleeps, its
// screens sleep or lock, or another user takes the session; and after the
// time without use Settings names (lockAfterMinutes). Pane is in use while
// it is in front and the Mac has input, which then is Pane's, and while a
// page plays sound or shows in picture-in-picture; behind, its clock runs
// from when it was last in front. No key monitor of its own (ADR 0079).
// Each calls BrowserController.lockNow, which does nothing without a PIN.
@MainActor
final class LockController {
	private unowned let controller: BrowserController
	// When Pane was last in front, for the time without use behind.
	private var leftFront = Date()
	private var observers: [(NotificationCenter, NSObjectProtocol)] = []
	private var timer: Timer?

	init(controller: BrowserController) {
		self.controller = controller

		let workspace = NSWorkspace.shared.notificationCenter

		for name in [NSWorkspace.willSleepNotification, NSWorkspace.screensDidSleepNotification, NSWorkspace.sessionDidResignActiveNotification] {
			observe(workspace, name) { $0.controller.lockNow() }
		}

		observe(DistributedNotificationCenter.default(), Notification.Name("com.apple.screenIsLocked")) { $0.controller.lockNow() }
		observe(NotificationCenter.default, NSApplication.didResignActiveNotification) { $0.leftFront = Date() }

		let timer = Timer(timeInterval: 15 / TestHooks.timeScale, repeats: true) { [weak self] _ in
			MainActor.assumeIsolated { self?.tick() }
		}
		// A tenth of slack, as Apple's energy guide asks of a repeating timer.
		timer.tolerance = timer.timeInterval / 10
		RunLoop.main.add(timer, forMode: .common)
		self.timer = timer
	}

	private func observe(_ center: NotificationCenter, _ name: Notification.Name, _ body: @escaping @MainActor (LockController) -> Void) {
		let observer = center.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
			MainActor.assumeIsolated {
				if let self {
					body(self)
				}
			}
		}

		observers.append((center, observer))
	}

	private func tick() {
		guard let minutes = controller.model.settings.lockAfterMinutes,
		      controller.lock.state.pin != nil, !controller.lock.isLocked, !inUse
		else {
			leftFront = Date()
			return
		}

		if idle * TestHooks.timeScale >= TimeInterval(minutes * 60) {
			controller.lockNow()
		}
	}

	// Seconds without use: since the Mac's last input while Pane is in
	// front, since Pane left the front otherwise.
	private var idle: TimeInterval {
		guard NSApp.isActive else {
			return Date().timeIntervalSince(leftFront)
		}

		leftFront = Date()
		return CGEventSource.secondsSinceLastEventType(.combinedSessionState, eventType: CGEventType(rawValue: ~0)!)
	}

	private var inUse: Bool {
		!controller.model.pictureInPictureTabIDs.isEmpty
			|| controller.model.profiles.contains { $0.tabs.contains { controller.page(for: $0.id)?.isAudible == true } }
	}
}
