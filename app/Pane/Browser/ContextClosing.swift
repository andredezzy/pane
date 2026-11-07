import PaneCore

// A profile's context the engine is opening (ProfileContexts.with: from the wait for
// the volume to the proxy set on it) or deleting. A lock that comes first
// waits for it: an opened context closes for that lock instead.
@MainActor
final class EngineWork {
	private(set) var lock: ContextClosing?

	func wait(for closing: ContextClosing) {
		lock = closing
		closing.expect()
	}
}

// What a lock waits for in the engine (ADR 0116): every profile's context
// closed, each one still opening as Pane locked once it has opened. `closed`
// says whether every profile closed, its files with it.
@MainActor
final class ContextClosing {
	private var remaining = 0
	private var everyClosed = true
	private var sealed = false
	private var result: Bool?
	private var waiting: [CheckedContinuation<Bool, Never>] = []

	// One more context to close.
	func expect() {
		remaining += 1
	}

	func close(_ context: BrowserContext, in application: BrowserApplication) {
		expect()
		application.closeContext(context) { [self] closed in
			finish(closed)
		}
	}

	// A context it expected closed, or never opened.
	func finish(_ closed: Bool) {
		everyClosed = everyClosed && closed
		remaining -= 1
		settle()
	}

	// Every context it waits for is named.
	func seal() {
		sealed = true
		settle()
	}

	var closed: Bool {
		get async {
			if let result {
				return result
			}

			return await withCheckedContinuation { waiting.append($0) }
		}
	}

	private func settle() {
		guard sealed, remaining == 0, result == nil else {
			return
		}

		result = everyClosed
		waiting.forEach { $0.resume(returning: everyClosed) }
		waiting = []
	}
}
