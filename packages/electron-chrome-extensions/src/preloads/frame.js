// Frame preload — patches missing APIs in extension page contexts.
// Registered as type:"frame" on each extension session.
// Runs in ALL extension pages: popups, options pages, app.html, etc.
//
// === Context Isolation ===
//
// Like sw.js, this preload runs in an ISOLATED JavaScript world. The extension
// page's own scripts run in the MAIN world where chrome lives. We cannot read
// or write chrome from here directly; instead we use:
//
//   contextBridge.exposeInMainWorld(key, obj)   — project __crxIpc into main world
//   contextBridge.executeInMainWorld({ func })  — run code IN the main world
//
// Functions passed to executeInMainWorld are serialised. They cannot close over
// variables defined in this preload file — they must be fully self-contained.
//
// Design note: this preload intentionally avoids replacing entire chrome
// namespaces. Overwriting a namespace object (e.g. chrome.tabs = {...}) causes
// Electron invariant violations because Electron's internal chrome bindings
// hold a direct reference to the original object. We only add or replace
// individual methods that are missing or wrong.

const { contextBridge, ipcRenderer } = require("electron");

// =============================================================================
// IPC Bridge — expose __crxIpc to the main world
// =============================================================================
//
// Extension page scripts call __crxIpc.invoke(namespace, method, ...args) to
// reach ECE's main-process handlers. ipcRenderer is only available here in the
// isolated world, so we project a thin wrapper into the main world.
//
// No fallback for !contextIsolated here (unlike sw.js) because extension pages
// in Pane always run with context isolation enabled.
if (process.contextIsolated) {
  contextBridge.exposeInMainWorld("__crxIpc", {
    invoke(namespace, method, ...args) {
      return ipcRenderer.invoke("crx-shim", namespace, method, ...args);
    },
  });
}

if ("executeInMainWorld" in contextBridge) {
  // ===========================================================================
  // Main-world patches — fix missing / broken chrome APIs
  // ===========================================================================
  //
  // All code below runs INSIDE the extension page's main world so it can
  // access and modify chrome. The function is self-contained; it relies only
  // on globalThis values set up by Electron or by exposeInMainWorld above.
  contextBridge.executeInMainWorld({
    func: function () {
      // -----------------------------------------------------------------------
      // process polyfill
      // -----------------------------------------------------------------------
      // Extension bundles frequently reference process.env.NODE_ENV. Node's
      // `process` is not available in a browser renderer context, so we provide
      // a minimal shim before any extension script reads it.
      if (typeof globalThis.process === "undefined") {
        globalThis.process = { env: { NODE_ENV: "production" }, platform: "darwin", version: "" };
      }

      var chrome = globalThis.chrome;
      if (!chrome) return;

      // -----------------------------------------------------------------------
      // chrome.extension stubs
      // -----------------------------------------------------------------------
      // Several chrome.extension methods are absent or throw in Electron's
      // chrome binding because they require browser internals (cross-renderer
      // view registry, profile incognito state). We patch only the missing ones
      // so existing functionality is not displaced.

      try {
        // getViews() normally returns live Window objects for other extension
        // pages. Electron has no such cross-renderer registry, so we return
        // an empty array rather than undefined/throw.
        if (chrome.extension && typeof chrome.extension.getViews !== "function") {
          chrome.extension.getViews = function () { return []; };
        }
      } catch (e) {}
      try {
        // isAllowedIncognitoAccess() checks the extension's manifest permission.
        // We always return false; Pane does not support incognito profiles.
        if (chrome.extension && typeof chrome.extension.isAllowedIncognitoAccess !== "function") {
          chrome.extension.isAllowedIncognitoAccess = function (cb) { if (cb) cb(false); return Promise.resolve(false); };
        }
      } catch (e) {}
      try {
        // isAllowedFileSchemeAccess() checks whether the extension may access
        // file:// URLs. Return false as a safe default.
        if (chrome.extension && typeof chrome.extension.isAllowedFileSchemeAccess !== "function") {
          chrome.extension.isAllowedFileSchemeAccess = function (cb) { if (cb) cb(false); return Promise.resolve(false); };
        }
      } catch (e) {}

      // -----------------------------------------------------------------------
      // Block window.close()
      // -----------------------------------------------------------------------
      // Extension popups call window.close() to dismiss themselves. In a real
      // browser this closes the popup window. In Electron the popup is a
      // BrowserView/WebContentsView managed by Pane — if the extension calls
      // window.close(), the underlying Electron window would be destroyed,
      // leaving the BrowserView in a broken state or crashing the app.
      //
      // We replace window.close with a no-op so popup scripts that call
      // window.close() after user interaction simply do nothing. Pane's own
      // UI handles hiding the popup at the right time.
      //
      // Exception: app.html is the extension's full-page fallback and may
      // legitimately need to close. The blank-popup detection below (in the
      // isolated world) uses ipcRenderer directly for that case, bypassing
      // this stub.
      globalThis.close = function () {};
      if (globalThis.window) globalThis.window.close = function () {};

      // -----------------------------------------------------------------------
      // IPC-backed chrome.windows.create and chrome.tabs.create
      // -----------------------------------------------------------------------
      // These APIs open new windows/tabs. In Electron the renderer has no
      // authority to spawn BrowserWindows directly — that must go through the
      // main process via IPC.
      //
      // windows.create: always replaced because the native binding either
      //   does nothing or throws in an extension renderer context.
      //
      // tabs.create: only patched when the native binding is missing. We do
      //   NOT replace an existing tabs.create because doing so would sever
      //   the native chrome.tabs binding. More critically: tabs is intentionally
      //   NOT fully shimmed here — a wholesale tabs replacement would break
      //   native methods like tabs.sendMessage that extensions depend on for
      //   messaging between their own components.
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

  // ===========================================================================
  // Blank popup fallback — open full extension page if popup renders nothing
  // ===========================================================================
  //
  // Some extensions show a loading spinner in their popup while the SW wakes
  // up, but never render actual content in the constrained popup viewport.
  // We detect this by counting DOM elements after a 1.5 s settle window.
  //
  // Why element count?
  //   A truly blank popup has only the minimal HTML skeleton: <html>, <head>,
  //   <body>, and perhaps one wrapper div — typically ≤ 3 elements inside
  //   #app or body. A real popup with rendered content will have many more.
  //   The threshold of 3 is intentionally conservative to avoid false positives
  //   on popups that render a single spinner or loading indicator.
  //
  // Why 1.5 s?
  //   Long enough for the SW to wake, fetch data, and render the first frame;
  //   short enough that users don't wait noticeably before the fallback opens.
  //
  // The fallback opens the extension's app.html (full-page view) in a new
  // window via ipcRenderer directly — bypassing the window.close() stub above
  // and the __crxIpc wrapper — because by this point the isolated-world relay
  // is the authoritative channel for creating windows.
  setTimeout(() => {
    // executeInMainWorld returns the value from the func synchronously (in
    // this Electron version). We read element count from the main world
    // because document is not accessible from the isolated world.
    const elCount = contextBridge.executeInMainWorld({
      func: function () {
        // Prefer #app as the extension's root mount point (React/Vue apps);
        // fall back to body. If neither exists return a high sentinel so we
        // don't incorrectly trigger the fallback on unusual page structures.
        var app = document.getElementById("app") || document.body;
        return app ? app.querySelectorAll("*").length : 999;
      },
    });

    if (elCount <= 3) {
      // Popup is blank — resolve the extension's full-page URL and open it.
      const extUrl = contextBridge.executeInMainWorld({
        func: function () {
          try { return globalThis.chrome?.runtime?.getURL?.("app.html"); } catch (e) { return null; }
        },
      });

      if (extUrl) {
        // Use ipcRenderer directly (not __crxIpc) to avoid any wrapping that
        // might be intercepted by the now-blocked window.close or other stubs.
        ipcRenderer.invoke("crx-shim", "windows", "create", { url: extUrl });
      }
    }
  }, 1500);
}
