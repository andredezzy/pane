// Service worker preload.
// For most extensions: just the process polyfill.
// For NordPass: also creates globalThis.browser with missing APIs.

const { ipcRenderer, contextBridge } = require("electron");

// Log to main process via IPC (SW console.log doesn't appear in terminal)
ipcRenderer.invoke("crx-shim", "__log", "sw.js preload loaded");

if ("executeInMainWorld" in contextBridge) {
  contextBridge.executeInMainWorld({
    func: function () {
      if (typeof globalThis.process === "undefined") {
        globalThis.process = { env: { NODE_ENV: "production" }, platform: "darwin", version: "" };
      }

      var oc = globalThis.chrome;
      if (!oc) return;

      // Only NordPass needs globalThis.browser. 1Password checks
      // globalThis.browser?.runtime?.id and exports it wholesale — breaks
      // if any Proxy exists. Dark Reader uses chrome.* directly.
      var id;
      try { id = oc.runtime && oc.runtime.id; } catch (e) {}
      if (id !== "eiaeiblijfjekdanodkjadfinkhbfgcd") return;

      // === NordPass-only below ===

      var ipc = globalThis.__crxIpc;

      function noop() {}
      function noopEvent() {
        return { addListener: noop, removeListener: noop, hasListener: function () { return false; }, hasListeners: function () { return false; } };
      }

      var eventMap = {};
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

      // Cache native API references once (stable, pre-corruption)
      var cached = {};
      var nativeKeys = ["runtime", "storage", "tabs", "scripting", "management", "extension", "webRequest", "i18n", "permissions"];
      for (var i = 0; i < nativeKeys.length; i++) {
        try { cached[nativeKeys[i]] = oc[nativeKeys[i]]; } catch (e) {}
      }

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
        contextMenus: {
          create: function () { return Promise.resolve(); }, update: noop,
          remove: function () { return Promise.resolve(); },
          removeAll: function () { return Promise.resolve(); },
          onClicked: noopEvent(),
        },
        idle: {
          setDetectionInterval: noop,
          queryState: function () { return Promise.resolve("active"); },
          onStateChanged: noopEvent(),
        },
        privacy: {
          network: { networkPredictionEnabled: { get: function () { return Promise.resolve({ value: false }); }, set: noop, clear: noop }, webRTCIPHandlingPolicy: { get: function () { return Promise.resolve({ value: "default" }); }, set: noop, clear: noop } },
          services: { autofillAddressEnabled: { get: function () { return Promise.resolve({ value: false }); }, set: noop, clear: noop }, autofillCreditCardEnabled: { get: function () { return Promise.resolve({ value: false }); }, set: noop, clear: noop }, passwordSavingEnabled: { get: function () { return Promise.resolve({ value: false }); }, set: noop, clear: noop } },
          websites: { hyperlinkAuditingEnabled: { get: function () { return Promise.resolve({ value: false }); }, set: noop, clear: noop } },
        },
        webNavigation: {
          getFrame: function () { return Promise.resolve(null); },
          getAllFrames: function () { return Promise.resolve([]); },
          onBeforeNavigate: noopEvent(), onCommitted: noopEvent(), onCompleted: noopEvent(),
          onCreatedNavigationTarget: noopEvent(), onDOMContentLoaded: noopEvent(),
          onErrorOccurred: noopEvent(), onHistoryStateUpdated: noopEvent(),
          onReferenceFragmentUpdated: noopEvent(), onTabReplaced: noopEvent(),
        },
        offscreen: {
          createDocument: function () { return Promise.resolve(); },
          closeDocument: function () { return Promise.resolve(); },
          hasDocument: function () { return Promise.resolve(false); },
        },
      };

      // NordPass accesses chrome.action.onClicked at the module level
      // (before it can assign this.browser = browser || chrome). So we must
      // replace globalThis.chrome with our Proxy for NordPass only.
      var proxy = new Proxy({}, {
        get: function (t, k) {
          if (k in extras) return extras[k];
          if (k in cached) return cached[k];
          try { return oc[k]; } catch (e) { return undefined; }
        },
        set: function (t, k, v) { return true; },
        has: function (t, k) { return (k in extras) || (k in cached); },
      });
      globalThis.browser = proxy;
      // NordPass accesses chrome.action.onClicked at the MODULE level —
      // before it can do `this.browser = browser || chrome`. The only way
      // to provide chrome.action.onClicked is to replace globalThis.chrome.
      // This is safe because we use Proxy({}, ...) (empty target, no V8
      // invariant violations) and cache native APIs pre-corruption.
      globalThis.chrome = proxy;
    },
  });
}
