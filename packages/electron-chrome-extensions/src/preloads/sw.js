/**
 * @file sw.js — Service Worker Preload
 *
 * Registered as `type: "service-worker"` on each extension session via
 * `session.registerPreloadScript()` in the ECE constructor. This script
 * runs BEFORE the extension's own SW script and BEFORE the upstream
 * `chrome-extension-api.preload.js` (registered as `crx-api-sw`).
 *
 * ## Purpose
 *
 * Provides three layers of infrastructure for extension service workers:
 *
 * 1. **IPC Bridge (`__crxIpc`)** — Projects a thin `ipcRenderer.invoke`
 *    wrapper into the main world so that shimmed Chrome APIs (alarms,
 *    windows, tabs, action) can reach the ECE main-process handlers via
 *    the `crx-shim` IPC channel.
 *
 * 2. **Event Relay (`crx-shim-event` → `__crxEvents`)** — Listens for
 *    `crx-shim-event` IPC messages from the main process (fired when
 *    alarms trigger, idle state changes, etc.) and dispatches them to
 *    listeners registered via `makeEvent()` in the main world.
 *
 * 3. **NordPass-specific API Proxy** — For the NordPass extension only,
 *    replaces `globalThis.chrome` with a Proxy that fills gaps in
 *    Electron's Chrome API surface (e.g. `chrome.action.onClicked`).
 *    Other extensions keep the native `chrome` object untouched.
 *
 * ## Two-World Architecture
 *
 * Electron's context isolation splits each renderer into two JS worlds:
 *
 *   **Isolated World** — where this preload script executes.
 *     - Has access to: `ipcRenderer`, `contextBridge`.
 *     - Does NOT have access to: `chrome`, the extension's `globalThis`.
 *
 *   **Main World** — where the extension SW script executes.
 *     - Has access to: `chrome`, `globalThis`.
 *     - Does NOT have access to: `ipcRenderer`, `contextBridge`.
 *
 * We cannot simply write `globalThis.browser = ...` in this preload
 * because this preload's `globalThis` is the isolated world's — invisible
 * to the extension script.
 *
 * Bridge tools used:
 *   - `contextBridge.exposeInMainWorld(key, obj)` — Safely copies a plain
 *     object from the isolated world into the main world's `globalThis`.
 *     Used to project `__crxIpc` so shimmed APIs can call `ipcRenderer`.
 *   - `contextBridge.executeInMainWorld({ func, args })` — Runs a
 *     self-contained function IN the main world. Used to create the
 *     browser Proxy and event dispatcher where `chrome` lives.
 *
 * **IMPORTANT:** Functions passed to `executeInMainWorld` are serialised
 * and run in a completely separate JS context. They CANNOT close over any
 * variable defined in this preload file (no lexical scope sharing). Every
 * value they need must be passed via the `args` array or read from the
 * main world's `globalThis`.
 *
 * ## Preload Ordering
 *
 * Service worker preloads are registered in this order:
 *   1. `crx-sw`     → this file (`sw.js`)
 *   2. `crx-api-sw` → `chrome-extension-api.preload.js`
 *
 * This ordering is critical: `sw.js` caches native `chrome.runtime`,
 * `chrome.storage`, etc. references from the UNCORRUPTED V8 Proxy.
 * Then `chrome-extension-api.preload.js` runs `injectExtensionAPIs()`
 * which calls `Object.defineProperty(chrome, ...)` — this silently
 * corrupts the V8 Proxy in SW contexts, but our cached references
 * remain valid.
 *
 * ## The V8 Proxy Problem
 *
 * In Electron's SW contexts, `globalThis.chrome` is a V8 host-defined
 * Proxy with non-configurable properties. These operations all fail:
 *   - `Object.defineProperty(chrome, ...)` — silently corrupts the Proxy
 *   - `chrome.newProp = value` — silently rejected by the set trap
 *   - `chrome.existingProp.newMethod = ...` — sub-objects are also immutable
 *   - `new Proxy(chrome, {...})` — V8 invariant violations after corruption
 *   - `Object.freeze(ourProxy)` when target is `{}` — kills the get trap
 *
 * The only safe approach is `new Proxy({}, {...})` with an empty target
 * and cached native references. This avoids all invariant checks because
 * the empty target has no non-configurable properties.
 */

const { ipcRenderer, contextBridge } = require("electron");

// =========================================================================
// §1 — IPC Bridge
// =========================================================================
//
// Extension SW scripts (via the Proxy extras defined below) call:
//
//   __crxIpc.invoke(namespace, method, ...args)
//
// to reach the ECE main-process shim handlers (alarms.ts, windows.ts,
// tabs.ts, etc.) via `ipcRenderer.invoke("crx-shim", ...)`.
//
// `ipcRenderer` is only available in the isolated world, so we project
// a thin wrapper into the main world under `globalThis.__crxIpc`.
//
// This bridge is exposed for ALL extension SWs, not just NordPass. The
// event relay (§2) and `__crxEvents` dispatcher (§3) also depend on it
// being available. Extensions that don't use it simply ignore it — it
// doesn't interfere with native `chrome.*` APIs.

if (process.contextIsolated) {
  contextBridge.exposeInMainWorld("__crxIpc", {
    invoke(namespace, method, ...args) {
      return ipcRenderer.invoke("crx-shim", namespace, method, ...args);
    },
  });
} else {
  // Fallback when context isolation is off (legacy/testing). This branch
  // should never be hit in production Electron builds.
  globalThis.__crxIpc = {
    invoke(namespace, method, ...args) {
      return ipcRenderer.invoke("crx-shim", namespace, method, ...args);
    },
  };
}

// =========================================================================
// §2 — Event Relay
// =========================================================================
//
// The ECE main process sends "crx-shim-event" IPC messages when browser
// events fire. Examples:
//   - alarms.ts fires `sw.send("crx-shim-event", "alarms", "onAlarm", detail)`
//   - idle.ts fires `sw.send("crx-shim-event", "idle", "onStateChanged", state)`
//
// This preload listens in the isolated world (the only place `ipcRenderer`
// is available) and forwards the event into the main world by calling
// `__crxEvents(namespace, eventName, data)` via `executeInMainWorld`.
//
// `executeInMainWorld` is used here because `__crxEvents` lives in the
// main world and is inaccessible from the isolated world. The function is
// self-contained — namespace, eventName, and the payload are passed as
// `args` rather than closed over.

ipcRenderer.on("crx-shim-event", function (_event, namespace, eventName, ...payload) {
  if ("executeInMainWorld" in contextBridge) {
    contextBridge.executeInMainWorld({
      func: function (ns, evt, data) {
        var d = globalThis.__crxEvents;
        if (d) d(ns, evt, data);
      },
      args: [namespace, eventName, payload.length === 1 ? payload[0] : payload],
    });
  } else {
    var d = globalThis.__crxEvents;
    if (d) d(namespace, eventName, payload.length === 1 ? payload[0] : payload);
  }
});

// =========================================================================
// §3 — Main-World Bootstrap
// =========================================================================
//
// Everything below runs INSIDE the SW's main world via `executeInMainWorld`.
// This is where `chrome` lives, so this is where:
//   - The event dispatcher (`__crxEvents`) is installed on `globalThis`
//   - The NordPass Proxy is built and assigned to `globalThis.chrome`
//
// The function receives no `args` — all construction is self-contained
// and reads from `globalThis` (where `__crxIpc` was projected by §1).

if ("executeInMainWorld" in contextBridge) {
  contextBridge.executeInMainWorld({
    func: function () {

      // -------------------------------------------------------------------
      // Process polyfill
      // -------------------------------------------------------------------
      // Extension bundles (Webpack/Rollup) reference `process.env.NODE_ENV`
      // at runtime. Node's `process` global doesn't exist in a browser/SW
      // context, so we provide a minimal shim before any extension code
      // reads it. The `getBuiltinModule` optional chain in NordPass's
      // background.js (line ~5170) also requires `process` to exist.

      if (typeof globalThis.process === "undefined") {
        globalThis.process = { env: { NODE_ENV: "production" }, platform: "darwin", version: "" };
      }

      var oc = globalThis.chrome;
      if (!oc) return;

      // __crxIpc was projected into this world by §1 via exposeInMainWorld.
      var ipc = globalThis.__crxIpc;

      // -------------------------------------------------------------------
      // Utility constructors
      // -------------------------------------------------------------------

      /** No-op placeholder for API methods we don't implement. */
      function noop() {}

      /** Returns a Chrome-style event object that silently discards listeners. */
      function noopEvent() {
        return {
          addListener: noop, removeListener: noop,
          hasListener: function () { return false; },
          hasListeners: function () { return false; },
        };
      }

      // -------------------------------------------------------------------
      // Event dispatcher — __crxEvents
      // -------------------------------------------------------------------
      // `__crxEvents(ns, evt, data)` is called by the isolated-world relay
      // (§2) whenever the main process fires an IPC event. It looks up the
      // listener array for the "namespace.eventName" key and calls each
      // registered callback. Errors in individual callbacks are swallowed
      // so a bad listener doesn't break the others.
      //
      // This dispatcher is shared across ALL extensions in the session.
      // The listeners are populated by `makeEvent()` which is only called
      // for NordPass (below the extension ID guard), so for other extensions
      // `eventMap` stays empty and `__crxEvents` is a harmless no-op.

      var eventMap = {};
      globalThis.__crxEvents = function (ns, evt, data) {
        var key = ns + "." + evt;
        var ls = eventMap[key];
        if (ls) ls.forEach(function (cb) { try { cb(data); } catch (e) {} });
      };

      /**
       * Creates a Chrome-style event object backed by `eventMap` so that
       * `__crxEvents` can find and invoke the right listeners by key.
       *
       * @param {string} ns        - Namespace (e.g. "alarms", "action")
       * @param {string} eventName - Event name (e.g. "onAlarm", "onClicked")
       * @returns {{ addListener, removeListener, hasListener, hasListeners }}
       */
      function makeEvent(ns, eventName) {
        var key = ns + "." + eventName;
        if (!eventMap[key]) eventMap[key] = [];
        var ls = eventMap[key];
        return {
          addListener: function (cb) { ls.push(cb); },
          removeListener: function (cb) { var i = ls.indexOf(cb); if (i !== -1) ls.splice(i, 1); },
          hasListener: function (cb) { return ls.indexOf(cb) !== -1; },
          hasListeners: function () { return ls.length > 0; },
        };
      }

      // -------------------------------------------------------------------
      // Extension-specific gating
      // -------------------------------------------------------------------
      //
      // Only NordPass needs `globalThis.browser` and `globalThis.chrome`
      // replaced with our Proxy. Setting `globalThis.browser` for other
      // extensions causes breakage:
      //
      //   1Password: checks `globalThis.browser?.runtime?.id` — if truthy,
      //     EXPORTS the entire browser object as its API module. ALL
      //     subsequent API calls go through our Proxy instead of native
      //     chrome, breaking native method bindings (different `this`
      //     context, V8 invariant issues).
      //
      //   Dark Reader: uses `chrome.*` directly (zero `globalThis.browser`
      //     references). Setting `globalThis.browser` is unnecessary and
      //     the typeof check for Firefox compatibility fails harmlessly.
      //
      // For non-NordPass extensions, the function returns here. They still
      // get `__crxEvents` (harmless no-op) and `__crxIpc` (unused).

      var id;
      try { id = oc.runtime && oc.runtime.id; } catch (e) {}
      if (id !== "eiaeiblijfjekdanodkjadfinkhbfgcd") return;

      // =================================================================
      // NordPass-only: cache natives + build Proxy + replace chrome
      // =================================================================
      //
      // NordPass requires special handling because:
      //
      //   1. It accesses `chrome.action.onClicked` at MODULE level
      //      (line 5148 of background.js) — before any class-level
      //      `this.browser = globalThis.browser || globalThis.chrome`
      //      assignment can run.
      //
      //   2. Electron's SW context does not provide `chrome.action`
      //      natively — it's `undefined`.
      //
      //   3. The V8 Proxy rejects ALL attempts to add it via assignment
      //      or `Object.defineProperty` (see file-level JSDoc above).
      //
      //   4. The only way to provide `chrome.action.onClicked` is to
      //      replace `globalThis.chrome` entirely with our own Proxy.
      //
      // Proxy design:
      //   - Target is `{}` (empty) to avoid V8 invariant violations.
      //     With a non-empty target, V8 checks that the get trap's return
      //     value matches non-configurable properties on the target —
      //     which fails because `chrome.runtime` returns different wrapper
      //     objects on each access after corruption.
      //   - Native APIs are served from pre-corruption cached references
      //     (captured below before `injectExtensionAPIs` corrupts chrome).
      //   - Extra APIs (action, alarms, windows, etc.) are provided as
      //     plain objects with IPC-backed methods.

      // -------------------------------------------------------------------
      // Cache native API references
      // -------------------------------------------------------------------
      // These are captured from the UNCORRUPTED V8 Proxy. After
      // `injectExtensionAPIs` runs (crx-api-sw, next in preload order),
      // `Object.defineProperty(chrome, ...)` corrupts the Proxy and
      // direct access to `chrome.runtime` etc. may throw. Our cached
      // references remain stable because they point to the original
      // namespace objects, not to the Proxy itself.

      var cached = {};
      var nativeKeys = [
        "runtime", "storage", "tabs", "scripting", "management",
        "extension", "webRequest", "i18n", "permissions",
      ];
      for (var i = 0; i < nativeKeys.length; i++) {
        try { cached[nativeKeys[i]] = oc[nativeKeys[i]]; } catch (e) {}
      }

      // -------------------------------------------------------------------
      // Extras: APIs absent from or broken in Electron's SW chrome
      // -------------------------------------------------------------------
      //
      // Each namespace below is either:
      //   (a) entirely absent from Electron's chrome (action, contextMenus,
      //       privacy, webNavigation, offscreen), or
      //   (b) needs IPC-backed implementations because the native version
      //       doesn't exist in SW context (alarms, idle, windows).
      //
      // Design note: `tabs` is intentionally NOT fully shimmed here.
      // A wholesale tabs replacement would break native methods like
      // `tabs.sendMessage` that extensions rely on for messaging. Only
      // specific methods that need IPC (windows.create, tabs.create) are
      // provided, and those live in the frame preload (frame.js).

      var extras = {
        // --- action / browserAction ---
        // Toolbar badge + popup APIs. Most badge methods are no-ops
        // (Electron doesn't render a real toolbar badge). `setPopup`
        // routes through IPC so ECE's BrowserActionAPI registers the
        // popup URL for the correct extension. `onClicked` is backed
        // by `makeEvent` so the main process can dispatch clicks.
        action: {
          setTitle: noop, getTitle: noop, setIcon: noop,
          setPopup: function (d) { if (ipc) ipc.invoke("action", "setPopup", d); },
          getPopup: noop,
          setBadgeText: noop, getBadgeText: noop,
          setBadgeBackgroundColor: noop, getBadgeBackgroundColor: noop,
          enable: noop, disable: noop, openPopup: noop,
          getUserSettings: function () { return Promise.resolve({ isOnToolbar: true }); },
          onClicked: makeEvent("action", "onClicked"),
          onUserSettingsChanged: noopEvent(),
        },

        // browserAction mirrors action for MV2 extensions.
        browserAction: {
          setTitle: noop, getTitle: noop, setIcon: noop,
          setPopup: function (d) { if (ipc) ipc.invoke("action", "setPopup", d); },
          getPopup: noop,
          setBadgeText: noop, getBadgeText: noop,
          setBadgeBackgroundColor: noop, getBadgeBackgroundColor: noop,
          enable: noop, disable: noop, openPopup: noop,
          getUserSettings: function () { return Promise.resolve({ isOnToolbar: true }); },
          onClicked: makeEvent("action", "onClicked"),
          onUserSettingsChanged: noopEvent(),
        },

        // --- alarms ---
        // Fully IPC-backed. The ECE main process (alarms.ts) owns alarm
        // scheduling via `setTimeout`/`setInterval` so alarms survive SW
        // suspension. When an alarm fires, alarms.ts sends a
        // `crx-shim-event` message that the relay (§2) dispatches to the
        // `onAlarm` listener here.
        alarms: {
          create: function (n, i) { return ipc ? ipc.invoke("alarms", "create", n, i) : Promise.resolve(); },
          get: function (n) { return ipc ? ipc.invoke("alarms", "get", n) : Promise.resolve(void 0); },
          getAll: function () { return ipc ? ipc.invoke("alarms", "getAll") : Promise.resolve([]); },
          clear: function (n) { return ipc ? ipc.invoke("alarms", "clear", n) : Promise.resolve(true); },
          clearAll: function () { return ipc ? ipc.invoke("alarms", "clearAll") : Promise.resolve(true); },
          onAlarm: makeEvent("alarms", "onAlarm"),
        },

        // --- idle ---
        // Delegates state queries to the main process (idle.ts) which
        // polls `powerMonitor.getSystemIdleTime()`. State changes are
        // pushed back via `crx-shim-event`.
        idle: {
          setDetectionInterval: function (s) { if (ipc) ipc.invoke("idle", "setDetectionInterval", s); },
          queryState: function (s) { return ipc ? ipc.invoke("idle", "queryState", s) : Promise.resolve("active"); },
          onStateChanged: makeEvent("idle", "onStateChanged"),
        },

        // --- windows ---
        // Fully IPC-backed. `BrowserWindow` management lives in the main
        // process; IPC is the only way to create/query/update windows
        // from a SW context. Handled by windows.ts.
        windows: {
          WINDOW_ID_NONE: -1,
          WINDOW_ID_CURRENT: -2,
          create: function (o) { return ipc ? ipc.invoke("windows", "create", o) : Promise.resolve({ id: 1 }); },
          get: function (id, o) { return ipc ? ipc.invoke("windows", "get", id, o) : Promise.resolve({ id: 1 }); },
          getCurrent: function (o) { return ipc ? ipc.invoke("windows", "getCurrent", o) : Promise.resolve({ id: 1 }); },
          getLastFocused: function (o) { return ipc ? ipc.invoke("windows", "getLastFocused", o) : Promise.resolve({ id: 1 }); },
          getAll: function (o) { return ipc ? ipc.invoke("windows", "getAll", o) : Promise.resolve([]); },
          update: function (id, i) { return ipc ? ipc.invoke("windows", "update", id, i) : Promise.resolve({ id: 1 }); },
          remove: function (id) { return ipc ? ipc.invoke("windows", "remove", id) : Promise.resolve(); },
          onCreated: noopEvent(),
          onRemoved: noopEvent(),
          onFocusChanged: noopEvent(),
        },

        // --- contextMenus ---
        // No-op stubs. Context menu rendering is host-app-controlled;
        // extensions cannot add real OS-level menu items from a SW.
        contextMenus: {
          create: function () { return Promise.resolve(); },
          update: noop,
          remove: function () { return Promise.resolve(); },
          removeAll: function () { return Promise.resolve(); },
          onClicked: noopEvent(),
        },

        // --- privacy ---
        // Stubs returning safe defaults. Extensions that read these
        // settings behave as if privacy-protective settings are active.
        privacy: {
          network: {
            networkPredictionEnabled: { get: function () { return Promise.resolve({ value: false }); }, set: noop, clear: noop },
            webRTCIPHandlingPolicy: { get: function () { return Promise.resolve({ value: "default" }); }, set: noop, clear: noop },
          },
          services: {
            autofillAddressEnabled: { get: function () { return Promise.resolve({ value: false }); }, set: noop, clear: noop },
            autofillCreditCardEnabled: { get: function () { return Promise.resolve({ value: false }); }, set: noop, clear: noop },
            passwordSavingEnabled: { get: function () { return Promise.resolve({ value: false }); }, set: noop, clear: noop },
          },
          websites: {
            hyperlinkAuditingEnabled: { get: function () { return Promise.resolve({ value: false }); }, set: noop, clear: noop },
          },
        },

        // --- webNavigation ---
        // Stubs. getFrame/getAllFrames return empty results; all events
        // are no-ops. Sufficient for extensions that check API existence
        // but don't depend on navigation event data.
        webNavigation: {
          getFrame: function () { return Promise.resolve(null); },
          getAllFrames: function () { return Promise.resolve([]); },
          onBeforeNavigate: noopEvent(),
          onCommitted: noopEvent(),
          onCompleted: noopEvent(),
          onCreatedNavigationTarget: noopEvent(),
          onDOMContentLoaded: noopEvent(),
          onErrorOccurred: noopEvent(),
          onHistoryStateUpdated: noopEvent(),
          onReferenceFragmentUpdated: noopEvent(),
          onTabReplaced: noopEvent(),
        },

        // --- offscreen ---
        // Stubs. Electron doesn't support offscreen documents; these
        // resolve cleanly so extensions don't throw on API access.
        offscreen: {
          createDocument: function () { return Promise.resolve(); },
          closeDocument: function () { return Promise.resolve(); },
          hasDocument: function () { return Promise.resolve(false); },
        },
      };

      // -------------------------------------------------------------------
      // Build the Proxy
      // -------------------------------------------------------------------
      //
      // Property resolution order:
      //   1. extras (our shimmed namespaces)
      //   2. cached (native APIs captured pre-corruption)
      //   3. oc[k]  (fallback to original chrome for anything else)
      //
      // Trap details:
      //   - `get`: resolves properties in the order above. Falls back to
      //     `oc[k]` wrapped in try/catch because some chrome properties
      //     throw when accessed outside a valid extension context.
      //   - `set`: always returns true (silently swallows writes). The
      //     V8 chrome Proxy rejects writes anyway; we match that behaviour
      //     without throwing.
      //   - `has`: reports extras and cached keys as present for `in`
      //     operator checks.
      //   - `defineProperty`: returns true without defining anything.
      //     This swallows `Object.defineProperty` calls from
      //     `injectExtensionAPIs` (crx-api-sw) which would otherwise
      //     pollute our empty target and potentially cause V8 invariant
      //     violations when the get trap returns a different value.
      //   - `getOwnPropertyDescriptor`: returns descriptors for extras
      //     and cached keys so that `Object.keys()` and property
      //     enumeration work correctly for shimmed namespaces.

      var proxy = new Proxy({}, {
        get: function (t, k) {
          if (k in extras) return extras[k];
          if (k in cached) return cached[k];
          try { return oc[k]; } catch (e) { return undefined; }
        },
        set: function () { return true; },
        has: function (t, k) {
          return (k in extras) || (k in cached);
        },
        defineProperty: function () { return true; },
        getOwnPropertyDescriptor: function (t, k) {
          if (k in extras) return { value: extras[k], writable: true, enumerable: true, configurable: true };
          if (k in cached) return { value: cached[k], writable: true, enumerable: true, configurable: true };
          return undefined;
        },
      });

      // Replace both globalThis.browser and globalThis.chrome so that
      // NordPass's `globalThis.browser || globalThis.chrome` picks up
      // our Proxy regardless of which path it takes.
      globalThis.browser = proxy;
      globalThis.chrome = proxy;
    },
  });
}
