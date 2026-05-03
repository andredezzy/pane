/**
 * @file idle.ts
 *
 * Implements the `chrome.idle` API for extension service workers running
 * inside Electron. Electron does not expose `chrome.idle` natively in SW
 * contexts, so this shim handles IPC calls forwarded from the SW preload.
 *
 * Idle detection is backed by Electron's `powerMonitor.getSystemIdleTime()`,
 * which returns the number of seconds the system input devices have been idle.
 * State is polled every {@link POLL_INTERVAL_MS} milliseconds rather than
 * driven by a native event, because `powerMonitor` does not emit granular
 * idle-threshold events. When the computed state changes, the `onStateChanged`
 * event is pushed to every extension's SW via `crx-shim-event`.
 *
 * State is scoped per Electron `Session` so that multiple browser profiles
 * can independently configure their detection intervals.
 */

import { powerMonitor, type Session } from "electron";

/** Matches the three states defined by the Chrome Idle API. */
type IdleState = "active" | "idle" | "locked";

/**
 * How often (in ms) to sample `powerMonitor.getSystemIdleTime()`.
 * 15 s is a reasonable balance between responsiveness and CPU overhead.
 */
const POLL_INTERVAL_MS = 15_000;

/** Per-session idle detection state. */
interface IdleSessionState {
	/** Seconds of inactivity before the system is considered idle. Defaults to 60. */
	detectionInterval: number;
	/** The last computed idle state; used to detect transitions and avoid redundant events. */
	lastState: IdleState;
	/** Handle for the active polling interval, or `null` if polling hasn't started. */
	pollTimer: ReturnType<typeof setInterval> | null;
}

/**
 * Top-level store keyed by `Session` instance.
 * Explicit cleanup is performed by {@link destroyIdle} on session teardown.
 */
const sessionState = new Map<Session, IdleSessionState>();

/**
 * Returns (or lazily creates) the {@link IdleSessionState} for the given session.
 *
 * @param ses - The Electron session owning the idle state.
 */
function getState(ses: Session): IdleSessionState {
	let state = sessionState.get(ses);

	if (!state) {
		state = { detectionInterval: 60, lastState: "active", pollTimer: null };
		sessionState.set(ses, state);
	}

	return state;
}

/**
 * Samples the current system idle time and maps it to a Chrome `IdleState`.
 *
 * Note: `powerMonitor` does not expose a "locked" state directly; detecting a
 * locked screen would require OS-level hooks. This implementation returns only
 * `"active"` or `"idle"`.
 *
 * @param detectionInterval - Seconds of inactivity that count as idle.
 */
function getCurrentState(detectionInterval: number): IdleState {
	return powerMonitor.getSystemIdleTime() >= detectionInterval
		? "idle"
		: "active";
}

/**
 * Starts the idle-state polling loop for the given session, if not already
 * running. The loop fires every {@link POLL_INTERVAL_MS} and pushes an
 * `onStateChanged` event to all extension SWs whenever the computed state
 * differs from the last observed state.
 *
 * Polling is started lazily on the first `setDetectionInterval` call so we
 * don't burn CPU for sessions that never use `chrome.idle`.
 *
 * @param ses - The Electron session to start polling for.
 */
function startPolling(ses: Session) {
	const state = getState(ses);

	if (state.pollTimer) {
		return; // Already polling — nothing to do.
	}

	state.pollTimer = setInterval(() => {
		const newState = getCurrentState(state.detectionInterval);

		// Only emit an event when the state actually changes to avoid flooding
		// extensions with redundant notifications.
		if (newState !== state.lastState) {
			state.lastState = newState;
			for (const ext of ses.extensions.getAllExtensions()) {
				const scope = `chrome-extension://${ext.id}/`;

				ses.serviceWorkers
					.startWorkerForScope(scope)
					.then((sw) =>
						sw.send("crx-shim-event", "idle", "onStateChanged", newState),
					)
					.catch(() => {});
			}
		}
	}, POLL_INTERVAL_MS);
}

/**
 * Handles all `chrome.idle.*` method calls forwarded from the SW preload.
 *
 * Supported methods:
 * - `setDetectionInterval` — configure the inactivity threshold and begin polling
 * - `queryState`           — synchronously return the current idle state
 *
 * @param ses    - The Electron session the call originates from.
 * @param method - The `chrome.idle` method name (e.g. `"queryState"`).
 * @param args   - Method arguments forwarded verbatim from the SW preload.
 * @returns      The return value to send back to the extension, or `undefined`.
 */
export function handleIdle(
	ses: Session,
	method: string,
	...args: unknown[]
): unknown {
	const state = getState(ses);

	switch (method) {
		case "setDetectionInterval": {
			const [intervalSec] = args as [number];
			state.detectionInterval = intervalSec;
			// Ensure polling is running so future state changes are observed.
			startPolling(ses);

			return undefined;
		}

		case "queryState": {
			// `queryState` uses its own threshold argument, independent of the
			// session-level `detectionInterval` set by `setDetectionInterval`.
			const [detectionIntervalSec] = args as [number];

			return getCurrentState(detectionIntervalSec);
		}

		default:
			return undefined;
	}
}

/**
 * Stops the polling interval and removes session state. Call this when the
 * session is being torn down (e.g. on profile close) to prevent timer leaks.
 *
 * @param ses - The Electron session whose idle state should be destroyed.
 */
export function destroyIdle(ses: Session) {
	const state = sessionState.get(ses);

	if (state?.pollTimer) {
		clearInterval(state.pollTimer);
	}

	sessionState.delete(ses);
}
