// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DESIGN_MENU_BUTTON_H_
#define PANE_UI_DESIGN_MENU_BUTTON_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "pane/ui/design/controls.h"
#include "pane/ui/design/icons.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/button/button.h"

namespace views {
class Label;
class MenuRunner;
}  // namespace views

namespace pane {

// The app's one menu of a flat list of options (the Swift's PaneMenuButton
// and its trigger, ADR 0065 and 0070): the option chosen, with its icon if it
// has one, then up and down chevrons, 32 points tall, in a 1-point border
// rounded 11, with no fill but the outline's hover (ADR 0063); 13 in from its
// start and 11 from its end, 8 between its parts. A press opens the system's
// menu of the options just under it, its leading edge on the trigger's, the
// option chosen checked, and the trigger keeps its hover's look while the
// menu shows. Assistive technologies read `label` as its name and the option
// as its value (ADR 0071).
class MenuButton : public views::Button, public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(MenuButton, views::Button)

 public:
  struct Option {
    std::u16string title;
    // Shown in the trigger beside the title, 14 points, its lines 1.75 of
    // the grid (Theme's), not in the menu.
    std::optional<Icon> icon;
  };

  // `chosen` hears the option's place once the menu has gone.
  MenuButton(std::u16string label,
             std::vector<Option> options,
             base::RepeatingCallback<void(size_t index)> chosen);
  MenuButton(const MenuButton&) = delete;
  MenuButton& operator=(const MenuButton&) = delete;
  ~MenuButton() override;

  // The option it shows, and the menu checks; none shows an empty title.
  void SetSelected(std::optional<size_t> index);
  std::optional<size_t> selected() const { return selected_; }
  // What the menu shows, for the channel to read and choose in, as a click
  // in the menu chooses.
  ui::SimpleMenuModel& menu_model() { return menu_model_; }
  // For a timing of a press to the system's menu: told as the press asks
  // for the menu, and as the menu is about to show.
  struct Opening {
    Opening();
    Opening(Opening&&);
    Opening& operator=(Opening&&);
    ~Opening();

    base::RepeatingClosure asked;
    base::RepeatingClosure shows;
  };
  void SetOpening(Opening opening);

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void PaintButtonContents(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;
  void OnMenuWillShow(ui::SimpleMenuModel* source) override;

 private:
  void ShowMenu(const ui::Event& event);
  void MenuGone();
  // The title's and the chevron's tints, as lit as the hover is.
  void Tint();
  // Where the icon, when it shows, and the chevron go.
  gfx::RectF IconBounds() const;
  gfx::RectF ChevronBounds() const;

  const std::vector<Option> options_;
  base::RepeatingCallback<void(size_t)> chosen_;
  std::optional<size_t> selected_;
  // SetSelected has run: an option, or none, shows.
  bool shown_once_ = false;
  const ControlLook look_;
  std::unique_ptr<ControlHighlight> highlight_;
  raw_ptr<views::Label> title_ = nullptr;
  ui::SimpleMenuModel menu_model_{this};
  std::unique_ptr<views::MenuRunner> menu_runner_;
  Opening opening_;
  base::WeakPtrFactory<MenuButton> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_MENU_BUTTON_H_
