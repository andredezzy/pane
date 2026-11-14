// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/page/page_web_view.h"

#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/metadata/metadata_impl_macros.h"

namespace pane {

bool PageWebView::SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) {
  content::RenderWidgetHostView* page =
      web_contents() ? web_contents()->GetRenderWidgetHostView() : nullptr;
  return page && page->IsShowing() && page->HasFocus() &&
         page->GetRenderWidgetHost()->GetProcess()->IsInitializedAndNotDead() &&
         views::WebView::SkipDefaultKeyEventProcessing(event);
}

BEGIN_METADATA(PageWebView)
END_METADATA

}  // namespace pane
