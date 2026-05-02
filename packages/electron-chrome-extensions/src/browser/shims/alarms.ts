/**
 * @file alarms.ts
 *
 * Implements the `chrome.alarms` API for extension service workers running
 * inside Electron. Electron does not expose `chrome.alarms` natively in SW
 * contexts, so this shim handles IPC calls forwarded from the SW preload and
 * backs them with Node's `setTimeout`/`clearTimeout`.
 *
 * Alarm state is scoped per Electron `Session` so that multiple independent
 * extension sessions (e.g. different browser profiles) cannot interfere with
 * each other.
 *
 * When an alarm fires, every extension registered in the session is woken via
 * `startWorkerForScope`, and the `alarms.onAlarm` event is delivered over the
 * `crx-shim-event` IPC channel so the SW preload can dispatch it to the
 * extension's JS context.
 */

import type { Session } from "electron";

/** A single scheduled alarm, including its backing Node timer handle. */
interface AlarmEntry {
  name: string;
  /** Node timer handle returned by `setTimeout`. */
  timer: ReturnType<typeof setTimeout>;
  /** Unix timestamp (ms) of the next scheduled fire. */
  scheduledTime: number;
  /** If set, the alarm repeats every this many minutes. */
  periodInMinutes?: number;
}

/** Per-session alarm registry. */
interface AlarmsState {
  alarms: Map<string, AlarmEntry>;
}

/**
 * Top-level store keyed by `Session` instance.
 * A `WeakMap` would be ideal for GC, but `Map` is used here to allow
 * explicit cleanup via {@link destroyAlarms}.
 */
const sessionState = new Map<Session, AlarmsState>();

/**
 * Returns (or lazily creates) the {@link AlarmsState} for the given session.
 *
 * @param ses - The Electron session owning the alarm registry.
 */
function getState(ses: Session): AlarmsState {
  let state = sessionState.get(ses);
  if (!state) {
    state = { alarms: new Map() };
    sessionState.set(ses, state);
  }
  return state;
}

/**
 * Fires an alarm by name: delivers the `onAlarm` event to every extension in
 * the session, then either reschedules a repeating alarm or removes it.
 *
 * The service worker may not be running at the time the alarm fires, so we
 * call `startWorkerForScope` to wake it before sending the event. Errors are
 * silently swallowed because a stopped/unregistered worker is a normal state.
 *
 * @param ses  - The Electron session owning the alarm.
 * @param name - The name of the alarm to fire.
 */
function fireAlarm(ses: Session, name: string) {
  const state = getState(ses);
  const entry = state.alarms.get(name);
  if (!entry) return;

  const detail = { name, scheduledTime: entry.scheduledTime };

  // Wake every extension's SW and push the onAlarm event.
  for (const ext of ses.extensions.getAllExtensions()) {
    const scope = `chrome-extension://${ext.id}/`;
    ses.serviceWorkers
      .startWorkerForScope(scope)
      .then((sw) => sw.send("crx-shim-event", "alarms", "onAlarm", detail))
      .catch(() => {});
  }

  if (entry.periodInMinutes) {
    // Repeating alarm: reschedule relative to now to avoid drift accumulation.
    const delayMs = entry.periodInMinutes * 60_000;
    entry.scheduledTime = Date.now() + delayMs;
    entry.timer = setTimeout(() => fireAlarm(ses, name), delayMs);
  } else {
    // One-shot alarm: remove it so `getAll` no longer returns it.
    state.alarms.delete(name);
  }
}

/**
 * Handles all `chrome.alarms.*` method calls forwarded from the SW preload.
 *
 * Supported methods mirror the Chrome extension Alarms API:
 * - `create`   — schedule a new (optionally repeating) alarm
 * - `get`      — retrieve a single alarm by name
 * - `getAll`   — retrieve all pending alarms
 * - `clear`    — cancel a single alarm by name
 * - `clearAll` — cancel all alarms
 *
 * @param ses    - The Electron session the call originates from.
 * @param method - The `chrome.alarms` method name (e.g. `"create"`).
 * @param args   - Method arguments forwarded verbatim from the SW preload.
 * @returns      The return value to send back to the extension, or `undefined`.
 */
export function handleAlarms(ses: Session, method: string, ...args: unknown[]): unknown {
  const state = getState(ses);

  switch (method) {
    case "create": {
      const [name, info] = args as [string, { delayInMinutes?: number; periodInMinutes?: number; when?: number }];

      // Clear any existing alarm with the same name before creating a new one,
      // matching Chrome's behaviour (create overwrites).
      const existing = state.alarms.get(name);
      if (existing) clearTimeout(existing.timer);

      // Resolve the initial delay: `when` is an absolute epoch ms, while
      // `delayInMinutes` and `periodInMinutes` are relative. For repeating
      // alarms with no explicit initial delay, fire after one period.
      let delayMs: number;
      if (info.when) {
        delayMs = Math.max(0, info.when - Date.now());
      } else if (info.delayInMinutes) {
        delayMs = info.delayInMinutes * 60_000;
      } else if (info.periodInMinutes) {
        delayMs = info.periodInMinutes * 60_000;
      } else {
        delayMs = 0;
      }

      const entry: AlarmEntry = {
        name,
        scheduledTime: Date.now() + delayMs,
        periodInMinutes: info.periodInMinutes,
        timer: setTimeout(() => fireAlarm(ses, name), delayMs),
      };
      state.alarms.set(name, entry);
      return undefined;
    }

    case "get": {
      const [name] = args as [string];
      const entry = state.alarms.get(name);
      if (!entry) return undefined;
      // Return a plain object matching the chrome.Alarm shape (no timer handle).
      return { name: entry.name, scheduledTime: entry.scheduledTime, periodInMinutes: entry.periodInMinutes };
    }

    case "getAll": {
      return [...state.alarms.values()].map((e) => ({
        name: e.name, scheduledTime: e.scheduledTime, periodInMinutes: e.periodInMinutes,
      }));
    }

    case "clear": {
      const [name] = args as [string];
      const entry = state.alarms.get(name);
      if (entry) {
        clearTimeout(entry.timer);
        state.alarms.delete(name);
      }
      // Chrome always returns true from clear(), even if the alarm didn't exist.
      return true;
    }

    case "clearAll": {
      for (const entry of state.alarms.values()) clearTimeout(entry.timer);
      state.alarms.clear();
      return true;
    }

    default:
      return undefined;
  }
}

/**
 * Cancels all pending alarms and removes session state. Call this when the
 * session is being torn down (e.g. on profile close) to prevent timer leaks.
 *
 * @param ses - The Electron session whose alarm state should be destroyed.
 */
export function destroyAlarms(ses: Session) {
  const state = sessionState.get(ses);
  if (!state) return;
  for (const entry of state.alarms.values()) clearTimeout(entry.timer);
  sessionState.delete(ses);
}
