// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/page/page_context_menu.h"

#include "chrome/browser/ui/views/renderer_context_menu/render_view_context_menu_views.h"

namespace pane {

// Chrome's own menu for Views, as ChromeWebContentsViewDelegateViews builds
// it.
// static
std::unique_ptr<RenderViewContextMenuBase> PageContextMenus::Make(
    content::RenderFrameHost& frame,
    const content::ContextMenuParams& params,
    bool paste,
    bool paste_and_match_style) {
  return RenderViewContextMenuViews::Create(frame, params, paste,
                                            paste_and_match_style);
}

// Views' menu runs in no loop of its own on Windows, its Show returning as it
// opens, where Run must return once it closes: showing it is W4's
// (docs/plans/views-migration.md). Built and filtered, it shows nothing yet.
// static
void PageContextMenus::Run(RenderViewContextMenuBase& menu,
                           base::OnceClosure tracking) {}

}  // namespace pane
