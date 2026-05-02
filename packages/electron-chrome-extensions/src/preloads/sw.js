// Service worker preload — provides the globalThis.browser Proxy and IPC bridge.
// Registered as type:"service-worker" on each extension session.
// Runs BEFORE any extension SW script.
//
// Architecture: The preload runs in an isolated context with access to ipcRenderer.
// The browser Proxy must live in the SW's main world (where chrome exists).
// We use contextBridge to bridge the two worlds:
// - exposeInMainWorld: exposes __crxIpc (IPC bridge) to the main world
// - executeInMainWorld: creates the browser Proxy + extras IN the main world

const { ipcRenderer, contextBridge } = require("electron");

// -- Expose IPC bridge to main world --
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

// -- Listen for events from main process and dispatch to main world --
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

// -- Create browser Proxy + extras in the main world --
if ("executeInMainWorld" in contextBridge) {
  contextBridge.executeInMainWorld({
    func: function () {
      // process global
      if (typeof globalThis.process === "undefined") {
        globalThis.process = { env: { NODE_ENV: "production" }, platform: "darwin", version: "" };
      }

      var oc = globalThis.chrome;
      if (!oc) return;

      var ipc = globalThis.__crxIpc;

      function noop() {}
      function noopEvent() {
        return {
          addListener: noop, removeListener: noop,
          hasListener: function () { return false; },
          hasListeners: function () { return false; },
        };
      }

      // Event dispatcher
      var eventMap = {};
      globalThis.__crxEvents = function (ns, evt, data) {
        var key = ns + "." + evt;
        var ls = eventMap[key];
        if (ls) ls.forEach(function (cb) { try { cb(data); } catch (e) {} });
      };

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

      // Extras map
      var extras = {
        action: {
          setTitle: noop, getTitle: noop, setIcon: noop,
          setPopup: function (d) { if (ipc) ipc.invoke("action", "setPopup", d); },
          getPopup: noop,
          setBadgeText: noop, getBadgeText: noop, setBadgeBackgroundColor: noop,
          getBadgeBackgroundColor: noop, enable: noop, disable: noop, openPopup: noop,
          getUserSettings: function () { return Promise.resolve({ isOnToolbar: true }); },
          onClicked: makeEvent("action", "onClicked"), onUserSettingsChanged: noopEvent(),
        },
        browserAction: {
          setTitle: noop, getTitle: noop, setIcon: noop,
          setPopup: function (d) { if (ipc) ipc.invoke("action", "setPopup", d); },
          getPopup: noop,
          setBadgeText: noop, getBadgeText: noop, setBadgeBackgroundColor: noop,
          getBadgeBackgroundColor: noop, enable: noop, disable: noop, openPopup: noop,
          getUserSettings: function () { return Promise.resolve({ isOnToolbar: true }); },
          onClicked: makeEvent("action", "onClicked"), onUserSettingsChanged: noopEvent(),
        },
        alarms: {
          create: function (n, i) { return ipc ? ipc.invoke("alarms", "create", n, i) : Promise.resolve(); },
          get: function (n) { return ipc ? ipc.invoke("alarms", "get", n) : Promise.resolve(void 0); },
          getAll: function () { return ipc ? ipc.invoke("alarms", "getAll") : Promise.resolve([]); },
          clear: function (n) { return ipc ? ipc.invoke("alarms", "clear", n) : Promise.resolve(true); },
          clearAll: function () { return ipc ? ipc.invoke("alarms", "clearAll") : Promise.resolve(true); },
          onAlarm: makeEvent("alarms", "onAlarm"),
        },
        idle: {
          setDetectionInterval: function (s) { if (ipc) ipc.invoke("idle", "setDetectionInterval", s); },
          queryState: function (s) { return ipc ? ipc.invoke("idle", "queryState", s) : Promise.resolve("active"); },
          onStateChanged: makeEvent("idle", "onStateChanged"),
        },
        windows: {
          WINDOW_ID_NONE: -1, WINDOW_ID_CURRENT: -2,
          create: function (o) { return ipc ? ipc.invoke("windows", "create", o) : Promise.resolve({ id: 1 }); },
          get: function (id, o) { return ipc ? ipc.invoke("windows", "get", id, o) : Promise.resolve({ id: 1 }); },
          getCurrent: function (o) { return ipc ? ipc.invoke("windows", "getCurrent", o) : Promise.resolve({ id: 1 }); },
          getLastFocused: function (o) { return ipc ? ipc.invoke("windows", "getLastFocused", o) : Promise.resolve({ id: 1 }); },
          getAll: function (o) { return ipc ? ipc.invoke("windows", "getAll", o) : Promise.resolve([]); },
          update: function (id, i) { return ipc ? ipc.invoke("windows", "update", id, i) : Promise.resolve({ id: 1 }); },
          remove: function (id) { return ipc ? ipc.invoke("windows", "remove", id) : Promise.resolve(); },
          onCreated: noopEvent(), onRemoved: noopEvent(), onFocusChanged: noopEvent(),
        },
        contextMenus: {
          create: function () { return Promise.resolve(); }, update: noop,
          remove: function () { return Promise.resolve(); },
          removeAll: function () { return Promise.resolve(); },
          onClicked: noopEvent(),
        },
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

      var patches = {
        extension: { getViews: function () { return []; } },
      };

      // globalThis.browser Proxy
      globalThis.browser = new Proxy(oc, {
        get: function (t, k) {
          if (k in extras) return extras[k];
          var native; try { native = t[k]; } catch (e) { native = undefined; }
          if (k in patches && native && typeof native === "object") {
            var merged = Object.create(Object.getPrototypeOf(native));
            try { var ns = Object.getOwnPropertyNames(native); for (var i = 0; i < ns.length; i++) { try { merged[ns[i]] = native[ns[i]]; } catch (e) {} } } catch (e) {}
            var pk = Object.keys(patches[k]); for (var j = 0; j < pk.length; j++) { merged[pk[j]] = patches[k][pk[j]]; }
            return merged;
          }
          return native;
        },
        set: function (t, k, v) { try { t[k] = v; } catch (e) {} return true; },
        has: function (t, k) { if (k in extras) return true; try { return k in t; } catch (e) { return false; } },
        getOwnPropertyDescriptor: function (t, k) {
          if (k in extras) return { value: extras[k], writable: true, enumerable: true, configurable: true };
          try { return Object.getOwnPropertyDescriptor(t, k); } catch (e) { return undefined; }
        },
        ownKeys: function (t) {
          try { var k = Reflect.ownKeys(t); Object.keys(extras).forEach(function (ek) { if (k.indexOf(ek) === -1) k.push(ek); }); return k; }
          catch (e) { return Object.keys(extras); }
        },
      });
    },
  });
}
