// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_PAGE_EXCLUSIVE_ACCESS_HINT_H_
#define PANE_UI_PAGE_EXCLUSIVE_ACCESS_HINT_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace pane {

// The full screen's Esc hint (ExclusiveAccessHint.swift, ADR 0025, Paper
// 04.25), in place of Chrome's bubble: what Chrome would say ("Press Esc to
// exit full screen"), at 13 medium on a 16-point line, white, on a capsule
// of black at 75%, 16 points in from its ends and 10 from its top and
// bottom, its top 24 down the window, in its middle. It shows at once and,
// 3.5 s later, fades (Motion.fade); it goes at once with the fullscreen, and
// a hint shown again comes back. It takes no press.
class ExclusiveAccessHint : public views::View {
  METADATA_HEADER(ExclusiveAccessHint, views::View)

 public:
  static constexpr int kTop = 24;

  ExclusiveAccessHint();
  ExclusiveAccessHint(const ExclusiveAccessHint&) = delete;
  ExclusiveAccessHint& operator=(const ExclusiveAccessHint&) = delete;
  ~ExclusiveAccessHint() override;

  // Shows `hint`, or goes, at once, for an empty one. The same hint again
  // changes nothing, as the Swift's task(id:) began again only for a new
  // text.
  void SetHint(const std::u16string& hint);
  const std::u16string& hint() const { return hint_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

 private:
  void FadeOut();

  std::u16string hint_;
  raw_ptr<views::Label> label_ = nullptr;
  base::OneShotTimer fade_;
};

}  // namespace pane

#endif  // PANE_UI_PAGE_EXCLUSIVE_ACCESS_HINT_H_
