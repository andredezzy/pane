# A crash loop shows the app's sad tab

When a tab's renderer crashes, the app reloads the page; when it crashes again within a minute, the app shows its own sad tab, with Reload, instead of reloading it in a loop. The rule is `model::CrashLoop` (`pane/model/crash_loop.h`), with tests: a second crash of the same tab within 60 seconds is a loop, another tab's crash is its own, and a tab released or closed starts over.

The sad tab is the app's, not Chrome's: a plain view in the page's place (`pane/ui/window/sad_tab_view.cc`), its column where the Swift app's was, to the pixel. The dead page leaves the place and the sad tab takes the keys from it, Return reloading while it has them. Its Reload, the toolbar's Reload, and ⌘R bring the page back, one intent for a crashed tab. A renderer's death reaches the controller a task later, so the reload never runs while Chrome's other observers are still being told the renderer died (ADR 0017).

`sad-tab` kills the selected tab's renderer and expects the page back, working. It then crashes a page twice in a row and expects the sad tab, and its Reload, or the toolbar's, pressed by real clicks where they show, to bring the page back.
