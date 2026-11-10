// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_SIDEBAR_TAB_ROW_H_
#define PANE_UI_SIDEBAR_TAB_ROW_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "pane/ui/sidebar/row_button.h"
#include "pane/ui/sidebar/sidebar_content.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/context_menu_controller.h"

namespace views {
class Label;
class MenuRunner;
}  // namespace views

namespace gfx {
class Canvas;
class ImageSkia;
class RectF;
}  // namespace gfx

namespace pane {

// What a tab's row and a drag's copy of it (H3) show: its title, or
// "Loading..." before it has one; and its favicon in its 16-point place,
// rounded as a favicon is (ADR 0057), at `opacity`, or the globe in `tint`
// without one.
std::u16string TabTitle(const SidebarTab& tab);
void PaintFavicon(gfx::Canvas& canvas,
                  const gfx::RectF& place,
                  const gfx::ImageSkia& favicon,
                  SkColor tint,
                  float opacity);

// A tab's row (the Swift's TabRow): its 16-point favicon, or a globe, 7 in,
// its title 10 after it at 12, and a close X that fades in with the row's
// hover. Selected, its fill and text show at once; without a page, its
// contents dim to 60%. A click selects it; its right-click menu, the Mac's
// own (Paper 03.2), closes it.
class TabRow : public RowButton,
               public views::ContextMenuController,
               public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(TabRow, RowButton)

 public:
  // What the row's presses ask, by its tab.
  struct Actions {
    Actions();
    Actions(Actions&&);
    Actions& operator=(Actions&&);
    ~Actions();

    base::RepeatingClosure select;
    base::RepeatingClosure close;
  };

  // The menu's commands.
  enum Command { kCloseTab = 1 };

  TabRow(const SidebarTab& tab, Actions actions);
  TabRow(const TabRow&) = delete;
  TabRow& operator=(const TabRow&) = delete;
  ~TabRow() override;

  void Update(const SidebarTab& tab);
  const SidebarTab& tab() const { return tab_; }
  // The menu its right click shows, which the channel reads and chooses
  // from.
  ui::SimpleMenuModel& menu_model() { return menu_model_; }

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void PaintButtonContents(gfx::Canvas* canvas) override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // ui::SimpleMenuModel::Delegate:
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  class CloseButton;

  // RowButton:
  void LookChanged() override;

  // The tab closes on the next turn: its page's going takes this row, which
  // its X's press, or the menu, is still on the stack of.
  void Close();

  SidebarTab tab_;
  const Actions actions_;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<CloseButton> close_ = nullptr;
  ui::SimpleMenuModel menu_model_{this};
  std::unique_ptr<views::MenuRunner> menu_runner_;
  base::WeakPtrFactory<TabRow> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_SIDEBAR_TAB_ROW_H_
