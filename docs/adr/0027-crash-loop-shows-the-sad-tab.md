# A crash loop shows the app's sad tab

When a tab's renderer crashes, the app reloads the page; when it crashes again within a minute, the app shows its own sad tab, with Reload, instead of reloading it in a loop. The rule is PaneKit's `CrashLoop` (`app/PaneKit/Sources/PaneKit/CrashLoop.swift`), with tests: a second crash of the same tab within 60 seconds is a loop, another tab's crash is its own, and a tab released or closed starts over.

The sad tab is the app's (`app/Pane/Views/SadTabView.swift`), not Chrome's. Its Reload, the toolbar's Reload, and ⌘R bring the page back. The engine posts a renderer's crash to the app a task later, so that every observer in Chrome sees the exit first: an app that reloaded inside `PrimaryMainFrameRenderProcessGone` relaunched the renderer while the others were still being told it died, and `extensions::RendererStartupHelper::OnRenderProcessLaunched` CHECK-failed (ADR 0017).

The `crash-recovery` scenario kills the selected tab's renderer and expects the page back, working. `sad-tab` crashes a page twice in a row and expects the sad tab, then its Reload, or the toolbar's, to bring the page back.
