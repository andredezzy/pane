# Pane is a native Swift shell on its own Chromium framework, not Electron or Tauri

Pane moves off Electron to the architecture Arc and Dia use: a pure-Swift app (AppKit + SwiftUI) that links Pane's own Chromium build, `PaneCore.framework`, in the same process through an Objective-C embedding API. The app's `main` calls `ChromeMain` and Chromium owns the run loop; each tab's native view sits in the app's own view hierarchy, so tabs of several profiles share one window; Chrome's `Browser` objects still exist behind every window but stay hidden, so extension APIs and Chrome internals keep working while every Chrome-owned UI surface is routed to the app as a delegate callback and drawn natively.

The framework is built from Chromium's `chrome/` layer plus a `//pane` tree, with a small patch series for the framework layout (ADR 0009), a host mode in which Chrome creates no UI of its own (ADR 0010), and the hook seams.

Scope is macOS on Apple Silicon; Windows and Linux are out for now (Dia ships on Mac only, and Arc's Windows port required porting Swift itself). The native app's data root is ADR 0006's, and the Electron data is migrated from a copy, never in place (ADR 0043). The product language in `CONTEXT.md`, the rules in `DESIGN.md`, and ADRs 0001 and 0002 carried over. Each subject since has an ADR of its own, from 0005.

What was rejected:

- **CEF:** embedding in the app's own NSView forces CEF's Alloy style, which has no extension system (removed in M128), no autofill or password manager, and no Chrome dialogs, covering 167 of the 1,701 methods Arc's app needs; the arc-engine analysis rejected it for Arc for the same reason.
- **The out-of-process design OpenAI uses for Atlas (OWL):** it stays a fallback only.
- **Staying on Electron:** Pane is a browser, Chromium is its substrate, and the shell is native, as Arc's and Dia's are. Pane had stayed on Electron only because the one alternative its July 2026 evaluation weighed was Tauri on WKWebView.
- **Tauri on WKWebView (July 2026):** weighed for its footprint and startup. Every headline feature sits on Chromium's session machinery with no WKWebView path: WKWebView has no extension runtime; fingerprint spoofing needs per-session main-world injection and header rewriting, and WebKit's own fingerprint would betray it; per-profile partitions and proxies need macOS 14 APIs that Tauri/wry does not expose; Google sign-in by CDP cookie import has no cookie-injection API, and Google refuses WKWebView. Tauri's multi-webview, which per-tab views need, is an unstable feature flag with open positioning and rendering bugs. The memory math failed too: the move could only shrink the shell (about 100–130 MB of a 342 MB light session, measured live), while tab content, which grows with use, is the engine's under any webview. Atrium, the closest precedent, tried a WKWebView pane through Tauri and retreated to CEF, adding about 170 MB back plus compositing hacks. Memory is served instead by tab sleep and profile unload (ADR 0041; `CONTEXT.md`: Sleep, Unload, Keep loaded). Revisit only if Pane's identity moves off Chromium-dependent features, or if Tauri stabilizes multi-webview *and* wry exposes per-webview data stores, proxies, and script injection, and even then re-run the memory math.

How it is checked:

- **`docs/benchmarks/native-vs-electron.md`** measures what the move bought: the native app at 0.11.2 against the last Electron release, 0.6.0, on one Mac, by the `benchmark-*` scenarios.
