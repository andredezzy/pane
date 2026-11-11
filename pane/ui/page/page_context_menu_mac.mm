// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/page/page_context_menu.h"

#import <AppKit/AppKit.h>

#include <utility>

#include "chrome/browser/ui/cocoa/renderer_context_menu/render_view_context_menu_mac_cocoa.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/native_ui_types.h"

namespace pane {

// Chrome's own menu for the Mac, an NSMenu over the page's view, as
// ChromeWebContentsViewDelegateViewsMac builds it for a window of the app's
// own process.
// static
std::unique_ptr<RenderViewContextMenuBase> PageContextMenus::Make(
    content::RenderFrameHost& frame,
    const content::ContextMenuParams& params,
    bool paste,
    bool paste_and_match_style) {
  content::WebContents* contents =
      content::WebContents::FromRenderFrameHost(&frame);
  content::RenderWidgetHostView* view =
      contents->GetTopLevelRenderWidgetHostView();
  NSView* parent = view ? view->GetNativeView().GetNativeNSView() : nil;
  return std::make_unique<RenderViewContextMenuMacCocoa>(
      frame, params, paste, paste_and_match_style, parent);
}

// static
void PageContextMenus::Run(RenderViewContextMenuBase& menu,
                           base::OnceClosure tracking) {
  // The menu shows once AppKit tracks it, the nearest Pane sees of the
  // system's window drawing it. Show returns once the menu has closed, so
  // `began` outlives the observer.
  auto began = std::make_unique<base::OnceClosure>(std::move(tracking));
  base::OnceClosure* once = began.get();
  id observer = [NSNotificationCenter.defaultCenter
      addObserverForName:NSMenuDidBeginTrackingNotification
                  object:nil
                   queue:nil
              usingBlock:^(NSNotification*) {
                if (*once) {
                  std::move(*once).Run();
                }
              }];
  menu.Show();
  [NSNotificationCenter.defaultCenter removeObserver:observer];
}

}  // namespace pane
