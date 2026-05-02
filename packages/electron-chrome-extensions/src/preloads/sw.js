// Service worker preload — provides the globalThis.browser Proxy and IPC bridge.
// Registered as type:"service-worker" on each extension session.
// Runs BEFORE any extension SW script.
//
// === Two-World Architecture ===
//
// Electron's context isolation splits each renderer into two JavaScript worlds:
//
//   [Isolated World]  — where this preload script runs.
//                       Has access to: ipcRenderer, contextBridge.
//                       Does NOT have access to: chrome, globalThis of the SW.
//
//   [Main World]      — where the SW script itself runs.
//                       Has access to: chrome, globalThis.
//                       Does NOT have access to: ipcRenderer, contextBridge.
//
// We cannot simply assign `globalThis.browser = ...` in the preload because
// the preload's globalThis is the isolated world's — invisible to the SW script.
//
// Bridge tools:
//   - contextBridge.exposeInMainWorld(key, obj): safely copies a plain object
//     into the main world's globalThis. Used for __crxIpc (IPC bridge).
//   - contextBridge.executeInMainWorld({ func, args }): runs a self-contained
//     function IN the main world. Used to create the browser Proxy + event
//     infrastructure directly where chrome lives.
//
// IMPORTANT: functions passed to executeInMainWorld are serialised and run in
// a completely separate JavaScript context. They CANNOT close over any variable
// defined in this preload file (no lexical scope sharing across worlds). Every
// value they need must be passed via the `args` array.

const { ipcRenderer, contextBridge } = require("electron");

// =============================================================================
// IPC Bridge — expose __crxIpc to the main world
// =============================================================================
//
// Extension SW scripts call __crxIpc.invoke(namespace, method, ...args) to
// reach the ECE main-process handlers (alarms, windows, tabs, etc.) via
// ipcRenderer.invoke("crx-shim", ...). ipcRenderer is only available in the
// isolated world, so we expose a thin wrapper into the main world.
//
// When context isolation is ON (normal Electron setup): exposeInMainWorld
// securely projects the object into the main world under window.__crxIpc.
//
// When context isolation is OFF (legacy/testing): we fall back to assigning
// directly on globalThis. This branch should never be hit in production.
if (process.contextIsolated) {
  contextBridge.exposeInMainWorld("__crxIpc", {
    invoke(namespace, method, ...args) {
      return ipcRenderer.invoke("crx-shim", namespace, method, ...args);
    },
  });
} else {
  globalThis.__crxIpc = {
    invoke(namespace, method, ...args) {
      return ipcRenderer.invoke("crx-shim", namespace, method, ...args);
    },
  };
}

// =============================================================================
// Event relay — forward crx-shim-event IPC messages into the main world
// =============================================================================
//
// The ECE main process emits "crx-shim-event" IPC messages when browser events
// fire (e.g. alarms.onAlarm, action.onClicked). The preload listens for these
// in the isolated world (the only place ipcRenderer is available) and calls
// __crxEvents in the main world to dispatch to registered listeners.
//
// executeInMainWorld is used here for the same reason as below: __crxEvents
// lives in the main world and is inaccessible from the isolated world.
// The function is self-contained — namespace, eventName, and the payload are
// passed as `args` rather than closed over.
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
    // Fallback when executeInMainWorld is unavailable (older Electron builds).
    var d = globalThis.__crxEvents;
    if (d) d(namespace, eventName, payload.length === 1 ? payload[0] : payload);
  }
});

// =============================================================================
// Main-world bootstrap — process polyfill, event dispatcher, browser Proxy
// =============================================================================
//
// Everything below runs INSIDE the SW's main world via executeInMainWorld.
// This is where chrome lives, so this is where browser must be created.
// The function receives no arguments — all construction is self-contained.
if ("executeInMainWorld" in contextBridge) {
  contextBridge.executeInMainWorld({
    func: function () {
      // -----------------------------------------------------------------------
      // process polyfill
      // -----------------------------------------------------------------------
      // Some extension bundles (Webpack/Rollup) reference process.env.NODE_ENV
      // at runtime. Node's `process` global doesn't exist in a browser/SW
      // context, so we provide a minimal shim before the SW script loads.
      if (typeof globalThis.process === "undefined") {
        globalThis.process = { env: { NODE_ENV: "production" }, platform: "darwin", version: "" };
      }

      // Guard: chrome must exist (this SW is on a chrome-compatible session).
      var oc = globalThis.chrome;
      if (!oc) return;

      // __crxIpc was injected by exposeInMainWorld above — available here.
      var ipc = globalThis.__crxIpc;

      // -----------------------------------------------------------------------
      // Utility stubs
      // -----------------------------------------------------------------------
      // noop: placeholder for API methods we intentionally do not implement
      //       (e.g. badge text in a non-browser environment is meaningless).
      function noop() {}

      // noopEvent: returns a no-op event object for API surface completeness.
      //            Listeners registered on these events simply never fire.
      function noopEvent() {
        return {
          addListener: noop, removeListener: noop,
          hasListener: function () { return false; },
          hasListeners: function () { return false; },
        };
      }

      // -----------------------------------------------------------------------
      // Event dispatcher — __crxEvents
      // -----------------------------------------------------------------------
      // __crxEvents(ns, evt, data) is called by the isolated-world relay above
      // (via executeInMainWorld) whenever the main process fires an IPC event.
      // It looks up the listener array for the "namespace.eventName" key and
      // calls each registered callback. Errors in individual callbacks are
      // swallowed so a bad listener doesn't break the others.
      var eventMap = {};
      globalThis.__crxEvents = function (ns, evt, data) {
        var key = ns + "." + evt;
        var ls = eventMap[key];
        if (ls) ls.forEach(function (cb) { try { cb(data); } catch (e) {} });
      };

      // makeEvent(ns, eventName) → chrome-style event object
      // Returns an object with the standard addListener/removeListener/hasListener
      // interface. Internally backed by a shared slot in eventMap so that
      // __crxEvents can find and invoke the right listeners by key.
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

      // -----------------------------------------------------------------------
      // Extras map — namespaces added to / replacing native chrome APIs
      // -----------------------------------------------------------------------
      // These namespaces are either absent from Electron's chrome object or
      // need IPC-backed implementations to function in a non-browser host.
      //
      // Design note: `tabs` is intentionally NOT shimmed here. A full tabs
      // shim would replace chrome.tabs entirely, breaking native methods like
      // tabs.sendMessage that extensions rely on for inter-component messaging.
      // Only the specific methods that need IPC (windows.create, tabs.create)
      // are patched individually.
      var extras = {
        // action / browserAction — toolbar badge + popup APIs.
        // Most badge methods are noops (we don't render a real toolbar badge).
        // setPopup and onClicked route through IPC / events so the host can
        // respond to popup-open requests.
        action: {
          setTitle: noop, getTitle: noop, setIcon: noop,
          setPopup: function (d) { if (ipc) ipc.invoke("action", "setPopup", d); },
          getPopup: noop,
          setBadgeText: noop, getBadgeText: noop, setBadgeBackgroundColor: noop,
          getBadgeBackgroundColor: noop, enable: noop, disable: noop, openPopup: noop,
          getUserSettings: function () { return Promise.resolve({ isOnToolbar: true }); },
          onClicked: makeEvent("action", "onClicked"), onUserSettingsChanged: noopEvent(),
        },
        // browserAction mirrors action for MV2 extensions that use the old API.
        browserAction: {
          setTitle: noop, getTitle: noop, setIcon: noop,
          setPopup: function (d) { if (ipc) ipc.invoke("action", "setPopup", d); },
          getPopup: noop,
          setBadgeText: noop, getBadgeText: noop, setBadgeBackgroundColor: noop,
          getBadgeBackgroundColor: noop, enable: noop, disable: noop, openPopup: noop,
          getUserSettings: function () { return Promise.resolve({ isOnToolbar: true }); },
          onClicked: makeEvent("action", "onClicked"), onUserSettingsChanged: noopEvent(),
        },
        // alarms — fully IPC-backed. The ECE main process owns alarm scheduling
        // (setTimeout/setInterval) so alarms survive SW suspension.
        alarms: {
          create: function (n, i) { return ipc ? ipc.invoke("alarms", "create", n, i) : Promise.resolve(); },
          get: function (n) { return ipc ? ipc.invoke("alarms", "get", n) : Promise.resolve(void 0); },
          getAll: function () { return ipc ? ipc.invoke("alarms", "getAll") : Promise.resolve([]); },
          clear: function (n) { return ipc ? ipc.invoke("alarms", "clear", n) : Promise.resolve(true); },
          clearAll: function () { return ipc ? ipc.invoke("alarms", "clearAll") : Promise.resolve(true); },
          onAlarm: makeEvent("alarms", "onAlarm"),
        },
        // idle — delegates state queries to the main process which has OS-level
        // idle detection. SW context cannot query system idle directly.
        idle: {
          setDetectionInterval: function (s) { if (ipc) ipc.invoke("idle", "setDetectionInterval", s); },
          queryState: function (s) { return ipc ? ipc.invoke("idle", "queryState", s) : Promise.resolve("active"); },
          onStateChanged: makeEvent("idle", "onStateChanged"),
        },
        // windows — fully IPC-backed. BrowserWindow management lives in the
        // main process; IPC is the only way to create/query/update windows.
        windows: {
          WINDOW_ID_NONE: -1, WINDOW_ID_CURRENT: -2,
          create: function (o) { return ipc ? ipc.invoke("windows", "create", o) : Promise.resolve({ id: 1 }); },
          get: function (id, o) { return ipc ? ipc.invoke("windows", "get", id, o) : Promise.resolve({ id: 1 }); },
          getCurrent: function (o) { return ipc ? ipc.invoke("windows", "getCurrent", o) : Promise.resolve({ id: 1 }); },
          getLastFocused: function (o) { return ipc ? ipc.invoke("windows", "getLastFocused", o) : Promise.resolve({ id: 1 }); },
          getAll: function (o) { return ipc ? ipc.invoke("windows", "getAll", o) : Promise.resolve([]); },
          update: function (id, i) { return ipc ? ipc.invoke("windows", "update", id, i) : Promise.resolve({ id: 1 }); },
          remove: function (id) { return ipc ? ipc.invoke("windows", "remove", id) : Promise.resolve(); },
          // Event stubs — real window lifecycle events are not wired here yet.
          onCreated: noopEvent(), onRemoved: noopEvent(), onFocusChanged: noopEvent(),
        },
        // contextMenus — no-op stubs. Context menu rendering is host-controlled
        // in Pane; extensions cannot add real OS-level menu items.
        contextMenus: {
          create: function () { return Promise.resolve(); }, update: noop,
          remove: function () { return Promise.resolve(); },
          removeAll: function () { return Promise.resolve(); },
          onClicked: noopEvent(),
        },
        // privacy — stubs returning safe defaults. Extensions that read these
        // settings should behave as if privacy-protective settings are in effect.
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
      };

      // -----------------------------------------------------------------------
      // Patches map — override specific methods on existing chrome namespaces
      // -----------------------------------------------------------------------
      // Used for methods that DO exist on the native chrome object but return
      // wrong results in an Electron host context. The Proxy merges these on
      // top of the native namespace object rather than replacing it wholesale,
      // preserving all other native methods.
      var patches = {
        // chrome.extension.getViews() normally returns other extension page
        // windows. In Electron there is no cross-renderer registry, so we
        // return an empty array rather than throwing.
        extension: { getViews: function () { return []; } },
      };

      // -----------------------------------------------------------------------
      // globalThis.browser — Proxy wrapping chrome with extras + patches
      // -----------------------------------------------------------------------
      // The Proxy intercepts property access on chrome and:
      //   1. Returns the extras object for known extra namespaces (action, etc.)
      //   2. For namespaces with patches, merges patch methods onto a shallow
      //      copy of the native namespace object so native methods survive.
      //   3. Falls through to native chrome for everything else.
      //
      // We wrap in a try/catch on native access because some chrome properties
      // throw when accessed outside a valid extension context.
      globalThis.browser = new Proxy(oc, {
        get: function (t, k) {
          if (k in extras) return extras[k];
          var native; try { native = t[k]; } catch (e) { native = undefined; }
          if (k in patches && native && typeof native === "object") {
            // Shallow-copy the native namespace then apply patch overrides.
            var merged = Object.create(Object.getPrototypeOf(native));
            try { var ns = Object.getOwnPropertyNames(native); for (var i = 0; i < ns.length; i++) { try { merged[ns[i]] = native[ns[i]]; } catch (e) {} } } catch (e) {}
            var pk = Object.keys(patches[k]); for (var j = 0; j < pk.length; j++) { merged[pk[j]] = patches[k][pk[j]]; }
            return merged;
          }
          return native;
        },
        // Allow extensions to write to chrome properties (e.g. chrome.storage).
        set: function (t, k, v) { try { t[k] = v; } catch (e) {} return true; },
        // `in` operator checks extras first, then native.
        has: function (t, k) { if (k in extras) return true; try { return k in t; } catch (e) { return false; } },
        // Makes Object.getOwnPropertyDescriptor work for extras keys.
        getOwnPropertyDescriptor: function (t, k) {
          if (k in extras) return { value: extras[k], writable: true, enumerable: true, configurable: true };
          try { return Object.getOwnPropertyDescriptor(t, k); } catch (e) { return undefined; }
        },
        // Merges extras keys with native own keys for for-in / Object.keys.
        ownKeys: function (t) {
          try { var k = Reflect.ownKeys(t); Object.keys(extras).forEach(function (ek) { if (k.indexOf(ek) === -1) k.push(ek); }); return k; }
          catch (e) { return Object.keys(extras); }
        },
      });
    },
  });
}
