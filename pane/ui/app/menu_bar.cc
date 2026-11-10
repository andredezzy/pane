// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/menu_bar.h"

#include <utility>

namespace pane {

MenuBar::MenuBar(CommandRunner& runner) : runner_(runner), root_(this) {
  for (const Menu& menu : MenuBarMenus()) {
    auto model = std::make_unique<ui::SimpleMenuModel>(this);
    for (const MenuItem& item : menu.items) {
      if (!item.command) {
        model->AddSeparator(ui::NORMAL_SEPARATOR);
        continue;
      }
      model->AddItem(static_cast<int>(*item.command), item.title);
      const size_t index = model->GetItemCount() - 1;
      if (item.accelerator.key_code() != ui::VKEY_UNKNOWN) {
        // The menu bar shows its key, as the platform's menus show theirs.
        model->SetAcceleratorAt(index, item.accelerator);
        model->SetForceShowAcceleratorForItemAt(index, true);
      }
      if (item.hidden) {
        model->SetVisibleAt(index, false);
      }
    }
    root_.AddSubMenu(/*command_id=*/0, menu.title, model.get());
    menus_.push_back(std::move(model));
  }
  native_ = Native::Show(root_);
}

MenuBar::~MenuBar() {
  // The platform's menus let go of the models first.
  native_.reset();
}

bool MenuBar::IsCommandIdEnabled(int command_id) const {
  // A menu's title, which always opens.
  if (command_id == 0) {
    return true;
  }
  return runner_->IsCommandEnabled(static_cast<Command>(command_id));
}

bool MenuBar::IsItemForCommandIdDynamic(int command_id) const {
  return command_id == static_cast<int>(Command::kFullScreen);
}

std::u16string MenuBar::GetLabelForCommandId(int command_id) const {
  const Command command = static_cast<Command>(command_id);
  if (std::optional<std::u16string> title = runner_->CommandTitle(command)) {
    return *title;
  }
  for (const Menu& menu : MenuBarMenus()) {
    for (const MenuItem& item : menu.items) {
      if (item.command == command) {
        return item.title;
      }
    }
  }
  return std::u16string();
}

void MenuBar::ExecuteCommand(int command_id, int event_flags) {
  runner_->RunCommand(static_cast<Command>(command_id));
}

}  // namespace pane
