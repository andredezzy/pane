// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_PAGE_PAGE_WEB_VIEW_H_
#define PANE_UI_PAGE_PAGE_WEB_VIEW_H_

#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/webview/webview.h"

namespace pane {

// A page's view: Views' WebView, but for whose keys come first. While a page
// holds the focus, WebView gives it every key before the window's shortcuts,
// which hear what the page leaves, as in Chrome (ADR 0019). A page that can't
// take a key yet never gives it back: a tab just reopened or woken has no view
// of its own until its renderer's first frame (WebView focuses it then,
// `WebView::SetUpNewMainFrame`), and that view stays hidden a moment more,
// while Chromium drops every key sent to a renderer whose process still starts
// (`RenderWidgetHostImpl::ForwardKeyboardEventWithCommands`), ahead of even the
// keys Pane reserves. Until the page shows, holds the focus, and runs, the
// window's shortcuts take the keys (on Windows, a key a hidden page can't take
// reaches them through the frame's `KeysToTheWindow`), as on the Mac, where
// its menu bar has them first.
class PageWebView : public views::WebView {
  METADATA_HEADER(PageWebView, views::WebView)

 public:
  using views::WebView::WebView;

  // views::View:
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override;
};

}  // namespace pane

#endif  // PANE_UI_PAGE_PAGE_WEB_VIEW_H_
