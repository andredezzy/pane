// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/extensions/extension_shortcuts.h"

#include "chrome/browser/extensions/commands/command_service.h"
#include "chrome/browser/profiles/profile.h"
#include "extensions/common/api/extension_action/action_info.h"
#include "extensions/common/command.h"
#include "extensions/common/extension.h"
#include "ui/base/accelerators/accelerator.h"

namespace pane {

ExtensionShortcuts::ExtensionShortcuts(Profile* profile,
                                       TabListInterface* tabs)
    : ExtensionKeybindingRegistry(profile, tabs, ALL_EXTENSIONS) {
  Init();
}

ExtensionShortcuts::~ExtensionShortcuts() = default;

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
  if (action && service->GetExtensionActionCommand(
                    extension->id(), action->type,
                    extensions::CommandService::ACTIVE, &command, nullptr) &&
      command.accelerator().key_code() != ui::VKEY_UNKNOWN) {
    (*commands)[command.command_name()] = command;
    found = true;
  }
  return found;
}

// The registry's own targets are the whole record: there is no focus manager
// to register with.
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
