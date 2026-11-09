// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_WINDOW_EMPTY_STATE_VIEW_H_
#define PANE_UI_WINDOW_EMPTY_STATE_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace pane {

// What the content panel shows with no tab selected, as the Electron app did
// (Paper 04.1): the Pane mark above one muted line, in the panel's middle,
// on its card. The mark takes the text's color, so it shows in light as in
// dark.
class EmptyStateView : public views::View {
  METADATA_HEADER(EmptyStateView, views::View)

 public:
  EmptyStateView();
  EmptyStateView(const EmptyStateView&) = delete;
  EmptyStateView& operator=(const EmptyStateView&) = delete;
  ~EmptyStateView() override;

  // views::View:
  void Layout(PassKey) override;

 private:
  raw_ptr<views::View> mark_ = nullptr;
  raw_ptr<views::Label> label_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_WINDOW_EMPTY_STATE_VIEW_H_
