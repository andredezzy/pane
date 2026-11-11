// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_SIDEBAR_ROW_BUTTON_H_
#define PANE_UI_SIDEBAR_ROW_BUTTON_H_

#include <memory>
#include <optional>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "pane/ui/design/controls.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/button/button.h"

namespace gfx {
class Point;
}  // namespace gfx

namespace views {
class MenuRunner;
}  // namespace views

namespace pane {

// A sidebar row (the Swift's RowButtonStyle): rounded 15, the accent fading in
// under the pointer as its text goes from muted to the accent's foreground; a
// press over it at once that fades as it lets go; the selected row's fill (the
// foreground at 5%) and text at once, with no hover fill of its own
// (DESIGN.md › Motion). Its contents dim to 60% for a tab without a page, its
// fill never. The pointer over a child (a tab's X) is over the row.
class RowButton : public views::Button {
  METADATA_HEADER(RowButton, views::Button)

 public:
  // How tall a row is: Aside's 32 points, 4 apart on its 36-point pitch.
  static constexpr int kHeight = 32;
  // Where a row's icon starts, and its title after the icon (ADR 0060).
  static constexpr int kLeading = 7;
  static constexpr int kTrailing = 8;
  static constexpr int kIconSize = 16;
  static constexpr int kIconSpacing = 10;

  explicit RowButton(PressedCallback pressed);
  RowButton(const RowButton&) = delete;
  RowButton& operator=(const RowButton&) = delete;
  ~RowButton() override;

  // The selected row's look, at once.
  void SetSelected(bool selected);
  bool selected() const { return selected_; }
  // New keeps its muted text under the pointer; the others darken.
  void SetTintsOnHover(bool tints_on_hover);
  // A tab without a page shows its contents at 60%.
  void SetDimmed(bool dimmed);
  // A drag carries the row (H3): it shows no hover or press of its own, the
  // copy over it has them (ADR 0066).
  void SetDragged(bool dragged);
  // Holds the row lit while its menu shows, as the Swift's row stayed under
  // its context menu; once the menu goes, the row follows the pointer.
  void SetMenuShowing(bool showing);
  // What the row's losing the mouse mid-press ends: a drag from it, as a
  // release would.
  void SetCaptureLost(base::RepeatingClosure capture_lost);

  // views::Button:
  void OnThemeChanged() override;
  void OnMouseCaptureLost() override;

  // How lit the row is, 0 to 1, as its hover fades.
  double hover() const;
  // The color its title and icon take now, at the contents' opacity.
  SkColor Tint() const;
  // The contents' opacity: 0.6 dimmed, else 1.
  float ContentsOpacity() const { return dimmed_ ? 0.6f : 1.0f; }
  // The row's right-click menu, which the channel reads and chooses from:
  // the rows that have one (MakeContextMenu).
  ui::SimpleMenuModel& menu_model() { return *menu_model_; }

 protected:
  // The row's look changed: a hover step, a press, the selection. Each row
  // retints what it draws.
  virtual void LookChanged();

  // A look of the row's own, for the profile's header (no fill when shown).
  void SetLook(const ControlLook& look);

  // The row's right-click menu, its commands answered by `delegate` (the row
  // itself): made once, for the row to fill.
  ui::SimpleMenuModel& MakeContextMenu(ui::SimpleMenuModel::Delegate* delegate);
  // Shows the menu at `point`, the row held lit under it until it goes
  // (SetMenuShowing).
  void RunContextMenu(const gfx::Point& point,
                      ui::mojom::MenuSourceType source_type);

 private:
  void UpdateLook();

  bool selected_ = false;
  bool tints_on_hover_ = true;
  bool dimmed_ = false;
  std::optional<ControlLook> own_look_;
  ControlLook look_;
  std::unique_ptr<ControlHighlight> highlight_;
  raw_ptr<ControlBackground> background_ = nullptr;
  base::RepeatingClosure capture_lost_;
  // The model before its runner, so a row deleted under its open menu cancels
  // the menu while the model stands, as the menu's controller requires
  // (ui/menus/cocoa/menu_controller.h; MenuModelAdapter likewise).
  std::unique_ptr<ui::SimpleMenuModel> menu_model_;
  std::unique_ptr<views::MenuRunner> menu_runner_;
  base::WeakPtrFactory<RowButton> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_SIDEBAR_ROW_BUTTON_H_
