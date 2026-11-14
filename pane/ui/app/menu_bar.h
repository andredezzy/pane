// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_MENU_BAR_H_
#define PANE_UI_APP_MENU_BAR_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ref.h"
#include "base/values.h"
#include "pane/ui/app/commands.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/menus/simple_menu_model.h"

namespace gfx {
class Point;
}

namespace views {
class Widget;
}

namespace pane {

// The menu bar (the Swift's MainMenu): one ui::SimpleMenuModel per menu, made
// from the command table, every item's state asked of the app (its runner)
// each time the menu shows or a key looks it up. The platform shows it: on
// the Mac as the app's main menu, through MenuControllerCocoa, the
// platform's own items running AppKit's actions (menu_bar_mac.mm); on
// Windows, which has no menu bar, as one menu under the sidebar's "Pane ⌄",
// Views' own (Paper 11.4's A, 11.5; menu_bar_win.cc).
class MenuBar : public ui::SimpleMenuModel::Delegate {
 public:
  // The platform's menu bar, which shows the models.
  class Native {
   public:
    // Shown as the app's, until it goes.
    static std::unique_ptr<Native> Show(ui::MenuModel& model);

    virtual ~Native() = default;

    // The item at `path` ("Pane", "Check for Updates…") chosen as a click
    // chooses it, the menus brought up to date first: whether it ran (false
    // when the menu disables it), or none when no item has that path.
    virtual std::optional<bool> Choose(
        const std::vector<std::u16string>& path) = 0;
    // The menus as they show now: each item's title, its key, and whether
    // it is enabled, hidden, or checked.
    virtual base::ListValue Describe() = 0;
    // Windows' one menu of the app's commands, over `widget`, its top left at
    // `anchor` (a point of the screen), `closed` running once it has gone:
    // whether it showed. The Mac's menu bar shows its own, and shows none.
    virtual bool ShowAppMenu(views::Widget& widget,
                             const gfx::Point& anchor,
                             ui::mojom::MenuSourceType source,
                             base::OnceClosure closed);
    // While it shows.
    virtual bool IsAppMenuShowing() const;
  };

  // `runner` outlives it.
  explicit MenuBar(CommandRunner& runner);
  MenuBar(const MenuBar&) = delete;
  MenuBar& operator=(const MenuBar&) = delete;
  ~MenuBar() override;

  // The menus, each a submenu of this one.
  ui::SimpleMenuModel& model() { return root_; }
  // Shown, while it lives; none where the platform has no menu bar yet.
  Native* native() { return native_.get(); }

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsItemForCommandIdDynamic(int command_id) const override;
  std::u16string GetLabelForCommandId(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  const raw_ref<CommandRunner> runner_;
  ui::SimpleMenuModel root_;
  std::vector<std::unique_ptr<ui::SimpleMenuModel>> menus_;
  std::unique_ptr<Native> native_;
};

}  // namespace pane

#endif  // PANE_UI_APP_MENU_BAR_H_
