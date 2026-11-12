// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/design/anchored_menu.h"

#include <utility>

#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/menu/menu_runner.h"

namespace pane {

namespace {

// Views' own menu, which shows where it is anchored, its runner closing it
// if it goes first.
class AnchoredMenuWin : public AnchoredMenu {
 public:
  AnchoredMenuWin(ui::MenuModel& model, base::RepeatingClosure gone)
      : runner_(&model, views::MenuRunner::CONTEXT_MENU, std::move(gone)) {}

  void Run(views::Widget& widget,
           const gfx::Point& anchor,
           ui::mojom::MenuSourceType source) override {
    runner_.RunMenuAt(&widget, nullptr, gfx::Rect(anchor, gfx::Size()),
                      views::MenuAnchorPosition::kTopLeft, source);
  }

  bool IsRunning() const override { return runner_.IsRunning(); }

 private:
  views::MenuRunner runner_;
};

}  // namespace

// static
std::unique_ptr<AnchoredMenu> AnchoredMenu::Make(ui::MenuModel& model,
                                                 base::RepeatingClosure gone) {
  return std::make_unique<AnchoredMenuWin>(model, std::move(gone));
}

}  // namespace pane
