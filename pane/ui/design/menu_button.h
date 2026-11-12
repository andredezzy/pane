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
}  // namespace views

namespace pane {

class AnchoredMenu;

// The app's one menu of options (the Swift's PaneMenuButton and its
// trigger, ADR 0065 and 0070): the option chosen, with its icon if it has
// one, then up and down chevrons, 32 points tall, in a 1-point border
// rounded 11, with no fill but the outline's hover (ADR 0063); 13 in from its
// start and 11 from its end, 8 between its parts. Embedded, it is a segment
// of a field, as tall as the field, in the field's border and corners (the
// profile sheet's proxy type): 12 in and 10 from its end, 6 between its
// parts, its chevrons 12 points and lighter, and as wide as its longest
// option, so what follows it in the field stays put as the choice changes. A
// press, a key, or an assistive technology opens the system's menu of the
// options just under it, its leading edge on the trigger's (AnchoredMenu),
// the option chosen checked, and the trigger keeps its hover's look while the
// menu shows. The options are a flat list, or grouped into submenus between
// separators (the profile sheet's time zones).
// Assistive technologies read `label` as its name and the option as its
// value (ADR 0071).
class MenuButton : public views::Button, public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(MenuButton, views::Button)

 public:
  struct Option {
    std::u16string title;
    // Shown in the trigger beside the title, 14 points, its lines 1.75 of
    // the grid (Theme's), not in the menu.
    std::optional<Icon> icon;
    // What the trigger shows for it, when not its title: a zone's city
    // ("Tokyo"), where the menu has "Tokyo (GMT+9)".
    std::u16string shown;
  };

  // An item of a menu laid out by hand: an option, by its place, a
  // separator, or a submenu of options.
  struct Entry {
    static Entry OptionAt(size_t index);
    static Entry Separator();
    static Entry Submenu(std::u16string title, std::vector<size_t> options);

    Entry();
    Entry(const Entry&);
    Entry& operator=(const Entry&);
    Entry(Entry&&);
    Entry& operator=(Entry&&);
    ~Entry();

    enum class Kind { kOption, kSeparator, kSubmenu } kind = Kind::kOption;
    size_t option = 0;
    std::u16string title;
    std::vector<size_t> options;
  };

  enum class Style { kSmall, kEmbedded };

  // `chosen` hears the option's place once the menu has gone.
  MenuButton(std::u16string label,
             std::vector<Option> options,
             base::RepeatingCallback<void(size_t index)> chosen,
             Style style = Style::kSmall);

  // Other options, laid out as `entries` (or one after another without),
  // the choice kept by its place while one is there.
  void SetOptions(std::vector<Option> options, std::vector<Entry> entries = {});
  const std::vector<Option>& options() const { return options_; }
  MenuButton(const MenuButton&) = delete;
  MenuButton& operator=(const MenuButton&) = delete;
  ~MenuButton() override;

  // The option it shows, and the menu checks; none shows an empty title.
  void SetSelected(std::optional<size_t> index);
  std::optional<size_t> selected() const { return selected_; }
  // What the menu shows, for the channel to read and choose in, as a click
  // in the menu chooses; a submenu is the model of its item.
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
  // The menu, from the options and their entries.
  void BuildMenu(const std::vector<Entry>& entries);
  // What the trigger shows for the option.
  const std::u16string& ShownTitle(size_t index) const;
  // The title's and the chevron's tints, as lit as the hover is.
  void Tint();
  // Where the icon, when it shows, and the chevron go.
  gfx::RectF IconBounds() const;
  gfx::RectF ChevronBounds() const;

  std::vector<Option> options_;
  base::RepeatingCallback<void(size_t)> chosen_;
  const Style style_;
  std::optional<size_t> selected_;
  // SetSelected has run: an option, or none, shows.
  bool shown_once_ = false;
  const ControlLook look_;
  std::unique_ptr<ControlHighlight> highlight_;
  raw_ptr<views::Label> title_ = nullptr;
  ui::SimpleMenuModel menu_model_{this};
  std::vector<std::unique_ptr<ui::SimpleMenuModel>> submenus_;
  // Embedded, the widest option's title, whose room it keeps.
  int widest_title_ = 0;
  // After the models it shows, so a trigger deleted under its open menu
  // closes the menu while they stand, as the menu's controller requires
  // (ui/menus/cocoa/menu_controller.h; MenuModelAdapter likewise).
  std::unique_ptr<AnchoredMenu> menu_;
  Opening opening_;
  base::WeakPtrFactory<MenuButton> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_DESIGN_MENU_BUTTON_H_
