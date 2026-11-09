// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_SIDEBAR_SIDEBAR_BUTTON_H_
#define PANE_UI_SIDEBAR_SIDEBAR_BUTTON_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "pane/ui/sidebar/row_button.h"

namespace gfx {
class Canvas;
class RectF;
}  // namespace gfx

namespace views {
class Label;
}

namespace pane {

// "+" in a 16-point place (the Swift's PlusMark): 18 points, light, in
// `color`.
void PaintPlusMark(gfx::Canvas& canvas, const gfx::RectF& place, SkColor color);

// A row of the sidebar's own (the Swift's SidebarButton and NewTabRow): its
// mark in the favicon's 16-point place, 7 in, and its title 10 after, at 12.
// New tab and New show a plus, Settings its gear.
class SidebarButton : public RowButton {
  METADATA_HEADER(SidebarButton, RowButton)

 public:
  enum class Mark { kPlus, kSettings };

  SidebarButton(PressedCallback pressed,
                Mark mark,
                const std::u16string& title);
  SidebarButton(const SidebarButton&) = delete;
  SidebarButton& operator=(const SidebarButton&) = delete;
  ~SidebarButton() override;

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  // RowButton:
  void LookChanged() override;

  const Mark mark_;
  raw_ptr<views::Label> title_ = nullptr;
};

}  // namespace pane

#endif  // PANE_UI_SIDEBAR_SIDEBAR_BUTTON_H_
