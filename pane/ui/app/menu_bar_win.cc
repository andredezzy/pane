// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/menu_bar.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "pane/ui/design/anchored_menu.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/models/menu_model.h"
#include "ui/gfx/geometry/point.h"
#include "ui/menus/simple_menu_model.h"

namespace pane {

namespace {

// The index of the item titled `title` in `model`, as the menu shows it now.
std::optional<size_t> IndexOf(ui::MenuModel& model,
                              const std::u16string& title) {
  for (size_t index = 0; index < model.GetItemCount(); ++index) {
    if (model.GetTypeAt(index) != ui::MenuModel::TYPE_SEPARATOR &&
        model.GetLabelAt(index) == title) {
      return index;
    }
  }
  return std::nullopt;
}

base::ListValue DescribeMenu(ui::MenuModel& model) {
  base::ListValue items;
  for (size_t index = 0; index < model.GetItemCount(); ++index) {
    if (model.GetTypeAt(index) == ui::MenuModel::TYPE_SEPARATOR) {
      continue;
    }
    ui::Accelerator accelerator;
    const bool has_key = model.GetAcceleratorAt(index, &accelerator);
    base::DictValue entry =
        base::DictValue()
            .Set("title", base::UTF16ToUTF8(model.GetLabelAt(index)))
            .Set("key", has_key
                            ? base::UTF16ToUTF8(accelerator.GetShortcutText())
                            : std::string())
            .Set("enabled", model.IsEnabledAt(index))
            .Set("hidden", !model.IsVisibleAt(index))
            .Set("checked", model.IsItemCheckedAt(index));
    if (ui::MenuModel* submenu = model.GetSubmenuModelAt(index)) {
      entry.Set("submenu", DescribeMenu(*submenu));
    }
    items.Append(std::move(entry));
  }
  return items;
}

// Windows' one menu (Paper 11.5): the Pane menu's own items, the six other
// menus as its submenus before its last group, Exit Pane, as Chrome's menu
// holds its own. Its items are the Pane menu's, each asked of it as the menu
// shows (enabled, titled, run), and its submenus are the menu bar's own
// models.
class AppMenuModel : public ui::SimpleMenuModel,
                     public ui::SimpleMenuModel::Delegate {
 public:
  explicit AppMenuModel(ui::MenuModel& root) : ui::SimpleMenuModel(this) {
    ui::MenuModel* pane = root.GetSubmenuModelAt(0);
    CHECK(pane);
    pane_ = pane;
    // The Pane menu's last separator, before Exit Pane.
    std::optional<size_t> last_separator;
    for (size_t index = 0; index < pane->GetItemCount(); ++index) {
      if (pane->GetTypeAt(index) == ui::MenuModel::TYPE_SEPARATOR) {
        last_separator = index;
      }
    }
    for (size_t index = 0; index < pane->GetItemCount(); ++index) {
      if (index == last_separator) {
        AddSeparator(ui::NORMAL_SEPARATOR);
        for (size_t menu = 1; menu < root.GetItemCount(); ++menu) {
          if (ui::MenuModel* submenu = root.GetSubmenuModelAt(menu)) {
            AddSubMenu(/*command_id=*/0, root.GetLabelAt(menu), submenu);
          }
        }
      }
      if (pane->GetTypeAt(index) == ui::MenuModel::TYPE_SEPARATOR) {
        AddSeparator(ui::NORMAL_SEPARATOR);
        continue;
      }
      AddItem(pane->GetCommandIdAt(index), pane->GetLabelAt(index));
      const size_t added = GetItemCount() - 1;
      ui::Accelerator accelerator;
      if (pane->GetAcceleratorAt(index, &accelerator)) {
        SetAcceleratorAt(added, accelerator);
        SetForceShowAcceleratorForItemAt(added, true);
      }
      SetVisibleAt(added, pane->IsVisibleAt(index));
    }
  }

  // ui::SimpleMenuModel::Delegate, the Pane menu's own answer for its items:
  // a submenu's title always opens.
  bool IsCommandIdEnabled(int command_id) const override {
    const std::optional<size_t> index = PaneIndexOf(command_id);
    return !index || pane_->IsEnabledAt(*index);
  }
  bool IsItemForCommandIdDynamic(int command_id) const override {
    return PaneIndexOf(command_id).has_value() &&
           pane_->IsItemDynamicAt(*PaneIndexOf(command_id));
  }
  std::u16string GetLabelForCommandId(int command_id) const override {
    const std::optional<size_t> index = PaneIndexOf(command_id);
    return index ? pane_->GetLabelAt(*index) : std::u16string();
  }
  void ExecuteCommand(int command_id, int event_flags) override {
    if (const std::optional<size_t> index = PaneIndexOf(command_id)) {
      pane_->ActivatedAt(*index, event_flags);
    }
  }

 private:
  // The shown item of the Pane menu that runs `command_id`.
  std::optional<size_t> PaneIndexOf(int command_id) const {
    if (command_id == 0) {
      return std::nullopt;
    }
    for (size_t index = 0; index < pane_->GetItemCount(); ++index) {
      if (pane_->GetCommandIdAt(index) == command_id &&
          pane_->IsVisibleAt(index)) {
        return index;
      }
    }
    return std::nullopt;
  }

  raw_ptr<ui::MenuModel> pane_ = nullptr;
};

// Windows has no menu bar: the menu bar's commands are one menu under the
// sidebar's "Pane ⌄" (Paper 11.4's A), Views' own as Chrome draws it on
// Windows (11.5), each key as Views writes Windows' (11.5.1). A scenario
// chooses its items by the Mac's menus' paths ("Pane", "Check for
// Updates…"; "File", "New Tab"), as a click chooses them.
class NativeMenuBarWin : public MenuBar::Native {
 public:
  explicit NativeMenuBarWin(ui::MenuModel& root)
      : root_(root), app_menu_(root) {}

  // MenuBar::Native:
  std::optional<bool> Choose(const std::vector<std::u16string>& path) override {
    if (path.size() != 2) {
      return std::nullopt;
    }
    const std::optional<size_t> menu = IndexOf(*root_, path[0]);
    ui::MenuModel* submenu = menu ? root_->GetSubmenuModelAt(*menu) : nullptr;
    if (!submenu) {
      return std::nullopt;
    }
    const std::optional<size_t> item = IndexOf(*submenu, path[1]);
    if (!item) {
      return std::nullopt;
    }
    if (!submenu->IsEnabledAt(*item)) {
      return false;
    }
    submenu->ActivatedAt(*item);
    return true;
  }

  base::ListValue Describe() override { return DescribeMenu(*root_); }

  bool ShowAppMenu(views::Widget& widget,
                   const gfx::Point& anchor,
                   ui::mojom::MenuSourceType source,
                   base::OnceClosure closed) override {
    if (IsAppMenuShowing()) {
      return false;
    }
    closed_ = std::move(closed);
    menu_ = AnchoredMenu::Make(
        app_menu_, base::BindRepeating(&NativeMenuBarWin::MenuClosed,
                                       weak_factory_.GetWeakPtr()));
    menu_->Run(widget, anchor, source);
    return true;
  }

  bool IsAppMenuShowing() const override { return menu_ && menu_->IsRunning(); }

 private:
  void MenuClosed() {
    if (closed_) {
      std::move(closed_).Run();
    }
  }

  const raw_ref<ui::MenuModel> root_;
  AppMenuModel app_menu_;
  std::unique_ptr<AnchoredMenu> menu_;
  base::OnceClosure closed_;
  base::WeakPtrFactory<NativeMenuBarWin> weak_factory_{this};
};

}  // namespace

// static
std::unique_ptr<MenuBar::Native> MenuBar::Native::Show(ui::MenuModel& model) {
  return std::make_unique<NativeMenuBarWin>(model);
}

}  // namespace pane
