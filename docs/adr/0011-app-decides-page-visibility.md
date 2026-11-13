# The app decides page visibility, in a layer-backed view tree

The app decides which page shows, as Chrome's tab strip does for its tabs: a tab's page is made hidden, like a background tab, and only the selected tab's page shows, in the window's one `views::WebView` (`pane/ui/window/root_view.cc`). Showing a tab (`Window::ShowTab`) activates it in its profile's hidden Browser (ADR 0014), as Chrome's tab strip would, and hands its page to that WebView, which shows it as Chrome's own browser view shows its active tab. A page that does not show is a background tab, which Chromium throttles.

The app shows the selected tab's page only on the browser page, and never behind the PIN lock or before the data opens (`UiApp::InteractionAllowed`; ADR 0042, ADR 0113); a lock while Pane is open closes every page (ADR 0116). Under Settings it is hidden (ADR 0073). A tab Chrome discarded loads again as it shows (ADR 0041).

The window's views and the page are layers of one compositor, as in Chrome's own windows. The root view paints the content panel under the page's WebView, whose page layer takes the panel's corners as radii (`SetNativeViewCornerRadii`, all four), so nothing masks the page (ADR 0073).

**Until 0.20.0,** the Swift app decided it through the engine's API (`PaneWebContents.visible`), which called `WasShown` and unhid `WebContentsViewCocoa`, hidden for contents created hidden, as Chrome's own view host does when it attaches a tab: without that explicit show, `WebContentsImpl` ignored occlusion updates until a page was first shown for real, so a page whose window started covered, or that opened while the screen was locked (ADR 0012), stayed blank. Its `PageHostView`, a layer-backed view, held the selected page and rounded its own layer.
