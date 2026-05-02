// Frame preload — patches missing APIs in extension page contexts.
// Registered as type:"frame" on each extension session.
// NEVER touches existing chrome Proxy properties (causes invariant violations).

const { contextBridge, ipcRenderer } = require("electron");

// Expose IPC bridge for frames
if (process.contextIsolated) {
  contextBridge.exposeInMainWorld("__crxIpc", {
    invoke(namespace, method, ...args) {
      return ipcRenderer.invoke("crx-shim", namespace, method, ...args);
    },
  });
}

if ("executeInMainWorld" in contextBridge) {
  contextBridge.executeInMainWorld({
    func: function () {
      if (typeof globalThis.process === "undefined") {
        globalThis.process = { env: { NODE_ENV: "production" }, platform: "darwin", version: "" };
      }

      var chrome = globalThis.chrome;
      if (!chrome) return;

      try {
        if (chrome.extension && typeof chrome.extension.getViews !== "function") {
          chrome.extension.getViews = function () { return []; };
        }
      } catch (e) {}
      try {
        if (chrome.extension && typeof chrome.extension.isAllowedIncognitoAccess !== "function") {
          chrome.extension.isAllowedIncognitoAccess = function (cb) { if (cb) cb(false); return Promise.resolve(false); };
        }
      } catch (e) {}
      try {
        if (chrome.extension && typeof chrome.extension.isAllowedFileSchemeAccess !== "function") {
          chrome.extension.isAllowedFileSchemeAccess = function (cb) { if (cb) cb(false); return Promise.resolve(false); };
        }
      } catch (e) {}

      // Block window.close()
      globalThis.close = function () {};
      if (globalThis.window) globalThis.window.close = function () {};

      // Patch chrome.windows.create and chrome.tabs.create with IPC
      var ipc = globalThis.__crxIpc;
      if (!ipc) return;
      try {
        if (chrome.windows) {
          chrome.windows.create = function (opts, cb) {
            var p = ipc.invoke("windows", "create", opts);
            if (cb) p.then(cb);
            return p;
          };
        }
      } catch (e) {}
      try {
        if (chrome.tabs && typeof chrome.tabs.create !== "function") {
          chrome.tabs.create = function (opts, cb) {
            var p = ipc.invoke("tabs", "create", opts);
            if (cb) p.then(cb);
            return p;
          };
        }
      } catch (e) {}
    },
  });

  // Blank popup fallback — detect empty popup and open extension's full page instead.
  setTimeout(() => {
    const elCount = contextBridge.executeInMainWorld({
      func: function () {
        var app = document.getElementById("app") || document.body;
        return app ? app.querySelectorAll("*").length : 999;
      },
    });

    if (elCount <= 3) {
      const extUrl = contextBridge.executeInMainWorld({
        func: function () {
          try { return globalThis.chrome?.runtime?.getURL?.("app.html"); } catch (e) { return null; }
        },
      });

      if (extUrl) {
        ipcRenderer.invoke("crx-shim", "windows", "create", { url: extUrl });
      }
    }
  }, 1500);
}
