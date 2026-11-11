// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/extensions/extension_shortcuts.h"

#include "chrome/browser/extensions/commands/command_service.h"
#include "chrome/browser/extensions/extension_commands_global_registry.h"
#include "chrome/browser/profiles/profile.h"
#include "extensions/common/api/extension_action/action_info.h"
#include "extensions/common/command.h"
#include "extensions/common/extension.h"
#include "ui/base/accelerators/accelerator.h"

namespace pane {

ExtensionShortcuts::ExtensionShortcuts(Profile* profile, TabListInterface* tabs)
    : ExtensionKeybindingRegistry(profile, tabs, ALL_EXTENSIONS),
      profile_(profile) {
  Init();
  // A profile has one Browser in Pane's window, so its registry is the
  // window's, as a Chrome window's registers as it activates: Chrome's
  // shortcuts page suspends it while one of its fields records.
  extensions::ExtensionCommandsGlobalRegistry::Get(profile)
      ->set_registry_for_active_window(this);
}

ExtensionShortcuts::~ExtensionShortcuts() {
  auto* global = extensions::ExtensionCommandsGlobalRegistry::Get(profile_);
  if (global && global->registry_for_active_window() == this) {
    global->set_registry_for_active_window(nullptr);
  }
}

bool ExtensionShortcuts::Find(const ui::Accelerator& accelerator,
                              extensions::ExtensionId* extension,
                              std::string* command) const {
  return GetFirstTarget(accelerator, extension, command);
}

bool ExtensionShortcuts::Notify(const ui::Accelerator& accelerator) {
  return NotifyEventTargets(accelerator);
}

// As Chrome's views registry, with the action's command beside the named
// ones.
bool ExtensionShortcuts::PopulateCommands(
    const extensions::Extension* extension,
    ui::CommandMap* commands) {
  auto* service = extensions::CommandService::Get(browser_context());
  bool found = service->GetNamedCommands(
      extension->id(), extensions::CommandService::ACTIVE,
      extensions::CommandService::REGULAR, commands);
  const extensions::ActionInfo* action =
      extensions::ActionInfo::GetExtensionActionInfo(extension);
  extensions::Command command;
  if (action &&
      service->GetExtensionActionCommand(extension->id(), action->type,
                                         extensions::CommandService::ACTIVE,
                                         &command, nullptr) &&
      command.accelerator().key_code() != ui::VKEY_UNKNOWN) {
    (*commands)[command.command_name()] = command;
    found = true;
  }
  return found;
}

// The registry's own targets are the whole record: no focus manager is
// registered with.
bool ExtensionShortcuts::RegisterAccelerator(
    const ui::Accelerator& accelerator,
    const extensions::ExtensionId& extension_id,
    const std::string& command_name) {
  return true;
}

bool ExtensionShortcuts::ShouldIgnoreCommand(const std::string& command) const {
  return false;
}

}  // namespace pane
