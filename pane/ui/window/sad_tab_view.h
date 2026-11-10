// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_WINDOW_SAD_TAB_VIEW_H_
#define PANE_UI_WINDOW_SAD_TAB_VIEW_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace pane {

class TextButton;

// What the page's place shows while the selected tab's page has crashed
// again within a minute of the reload of its first crash, or its profile
// would not open (ADR 0027): the Swift's SadTabView, in the content panel's
// style, on its card. "This page crashed" at the page title's size over the
// empty state's muted line, and an outline Reload, centered. It takes the
// keys as the page whose place it shows would, and Return reloads, as the
// Swift's default action did.
class SadTabView : public views::View {
  METADATA_HEADER(SadTabView, views::View)

 public:
  explicit SadTabView(base::RepeatingClosure reload);
  SadTabView(const SadTabView&) = delete;
  SadTabView& operator=(const SadTabView&) = delete;
  ~SadTabView() override;

  // views::View:
  void Layout(PassKey) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;

 private:
  base::RepeatingClosure reload_;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> message_ = nullptr;
  raw_ptr<TextButton> button_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_WINDOW_SAD_TAB_VIEW_H_
