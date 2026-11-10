# A crash loop shows the app's sad tab

> **On the Views line** (`docs/plans/views-migration.md`, H5): the sad tab is a plain view in the page's place (`pane/ui/window/sad_tab_view.cc`), its column on SwiftUI's pixels; the dead page leaves the place and the sad tab takes the keys from it, Return reloading while it has them, as the Swift's default action did; its Reload is the toolbar's, one intent for a crashed tab. `sad-tab` presses both by real clicks where they show.

When a tab's renderer crashes, the app reloads the page; when it crashes again within a minute, the app shows its own sad tab, with Reload, instead of reloading it in a loop. The rule is PaneKit's `CrashLoop` (`app/PaneKit/Sources/PaneKit/CrashLoop.swift`), with tests: a second crash of the same tab within 60 seconds is a loop, another tab's crash is its own, and a tab released or closed starts over.

The sad tab is the app's (`app/Pane/Views/SadTabView.swift`), not Chrome's. Its Reload, the toolbar's Reload, and ⌘R bring the page back. The engine posts a renderer's crash to the app a task later, so the app's reload never runs while Chrome's other observers are still being told the renderer died (ADR 0017).

`sad-tab` kills the selected tab's renderer and expects the page back, working. `sad-tab` crashes a page twice in a row and expects the sad tab, then its Reload, or the toolbar's, to bring the page back.
