import { powerMonitor, type Session } from "electron";

type IdleState = "active" | "idle" | "locked";

const POLL_INTERVAL_MS = 15_000;

interface IdleSessionState {
  detectionInterval: number;
  lastState: IdleState;
  pollTimer: ReturnType<typeof setInterval> | null;
}

const sessionState = new Map<Session, IdleSessionState>();

function getState(ses: Session): IdleSessionState {
  let state = sessionState.get(ses);
  if (!state) {
    state = { detectionInterval: 60, lastState: "active", pollTimer: null };
    sessionState.set(ses, state);
  }
  return state;
}

function getCurrentState(detectionInterval: number): IdleState {
  return powerMonitor.getSystemIdleTime() >= detectionInterval ? "idle" : "active";
}

function startPolling(ses: Session) {
  const state = getState(ses);
  if (state.pollTimer) return;

  state.pollTimer = setInterval(() => {
    const newState = getCurrentState(state.detectionInterval);
    if (newState !== state.lastState) {
      state.lastState = newState;
      for (const ext of ses.extensions.getAllExtensions()) {
        const scope = `chrome-extension://${ext.id}/`;
        ses.serviceWorkers
          .startWorkerForScope(scope)
          .then((sw) => sw.send("crx-shim-event", "idle", "onStateChanged", newState))
          .catch(() => {});
      }
    }
  }, POLL_INTERVAL_MS);
}

export function handleIdle(ses: Session, method: string, ...args: unknown[]): unknown {
  const state = getState(ses);

  switch (method) {
    case "setDetectionInterval": {
      const [intervalSec] = args as [number];
      state.detectionInterval = intervalSec;
      startPolling(ses);
      return undefined;
    }

    case "queryState": {
      const [detectionIntervalSec] = args as [number];
      return getCurrentState(detectionIntervalSec);
    }

    default:
      return undefined;
  }
}

export function destroyIdle(ses: Session) {
  const state = sessionState.get(ses);
  if (state?.pollTimer) clearInterval(state.pollTimer);
  sessionState.delete(ses);
}
