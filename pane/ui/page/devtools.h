// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_PAGE_DEVTOOLS_H_
#define PANE_UI_PAGE_DEVTOOLS_H_

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "ui/gfx/geometry/rect.h"

namespace content {
class WebContents;
}

namespace ui {
class Compositor;
}

namespace pane {

// DevTools docked to a page, as Chrome's DevtoolsUIController lays them out
// in its contents container (ADR 0024): they fill the page's place, and the
// page sits over them where their splitter puts it, or nowhere while they
// cover it (ApplyDevToolsContentsResizingStrategy). Without DevTools, the
// page fills its place alone.
struct DockedDevTools {
  raw_ptr<content::WebContents> devtools = nullptr;
  gfx::Rect devtools_bounds;
  gfx::Rect page_bounds;
};
DockedDevTools DockedDevToolsFor(content::WebContents* page,
                                 const gfx::Rect& place);

// Developer Tools (⇧⌘I, the Swift's toggleDevTools): DevTools docked to
// `page` close, as their own close button closes them; otherwise they open
// as they last were, docked or in their own window (brought forward if
// open, where the Swift's key did nothing), timed to the frame of
// `compositor()` that shows their first paint.
void ToggleDevTools(content::WebContents* page,
                    base::RepeatingCallback<::ui::Compositor*()> compositor);

}  // namespace pane

#endif  // PANE_UI_PAGE_DEVTOOLS_H_
