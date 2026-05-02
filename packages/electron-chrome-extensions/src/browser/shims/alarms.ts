import type { Session } from "electron";

interface AlarmEntry {
  name: string;
  timer: ReturnType<typeof setTimeout>;
  scheduledTime: number;
  periodInMinutes?: number;
}

interface AlarmsState {
  alarms: Map<string, AlarmEntry>;
}

const sessionState = new Map<Session, AlarmsState>();

function getState(ses: Session): AlarmsState {
  let state = sessionState.get(ses);
  if (!state) {
    state = { alarms: new Map() };
    sessionState.set(ses, state);
  }
  return state;
}

function fireAlarm(ses: Session, name: string) {
  const state = getState(ses);
  const entry = state.alarms.get(name);
  if (!entry) return;

  const detail = { name, scheduledTime: entry.scheduledTime };

  for (const ext of ses.extensions.getAllExtensions()) {
    const scope = `chrome-extension://${ext.id}/`;
    ses.serviceWorkers
      .startWorkerForScope(scope)
      .then((sw) => sw.send("crx-shim-event", "alarms", "onAlarm", detail))
      .catch(() => {});
  }

  if (entry.periodInMinutes) {
    const delayMs = entry.periodInMinutes * 60_000;
    entry.scheduledTime = Date.now() + delayMs;
    entry.timer = setTimeout(() => fireAlarm(ses, name), delayMs);
  } else {
    state.alarms.delete(name);
  }
}

export function handleAlarms(ses: Session, method: string, ...args: unknown[]): unknown {
  const state = getState(ses);

  switch (method) {
    case "create": {
      const [name, info] = args as [string, { delayInMinutes?: number; periodInMinutes?: number; when?: number }];
      const existing = state.alarms.get(name);
      if (existing) clearTimeout(existing.timer);

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

export function destroyAlarms(ses: Session) {
  const state = sessionState.get(ses);
  if (!state) return;
  for (const entry of state.alarms.values()) clearTimeout(entry.timer);
  sessionState.delete(ses);
}
