// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/extensions/extension_shortcuts.h"

#include <utility>

#include "base/scoped_observation.h"
#include "chrome/browser/extensions/commands/command_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/generated_resources.h"
#include "extensions/common/api/extension_action/action_info.h"
#include "extensions/common/command.h"
#include "extensions/common/extension.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/l10n/l10n_util.h"

namespace pane {

namespace {

// The commands that lose their shortcut while it lives, as CommandService
// reports them.
class RemovedCommands : public extensions::CommandService::Observer {
 public:
  explicit RemovedCommands(extensions::CommandService* service) {
    observation_.Observe(service);
  }

  const std::vector<std::pair<extensions::ExtensionId, std::string>>& list()
      const {
    return list_;
  }

 private:
  // extensions::CommandService::Observer:
  void OnExtensionCommandRemoved(const extensions::ExtensionId& extension,
                                 const std::string& command) override {
    list_.emplace_back(extension, command);
  }

  std::vector<std::pair<extensions::ExtensionId, std::string>> list_;
  base::ScopedObservation<extensions::CommandService,
                          extensions::CommandService::Observer>
      observation_{this};
};

}  // namespace

std::vector<ui::Command> CommandsOf(Profile* profile,
                                    const extensions::Extension& extension) {
  auto* service = extensions::CommandService::Get(profile);
  std::vector<ui::Command> commands;
  // What the command's key is now: its manifest's suggestion only once
  // granted, the user's once changed.
  auto add = [&](ui::Command command) {
    command.set_accelerator(
        service->FindCommandByName(extension.id(), command.command_name())
            .accelerator());
    commands.push_back(std::move(command));
  };
  for (auto type :
       {extensions::ActionInfo::Type::kBrowser,
        extensions::ActionInfo::Type::kPage,
        extensions::ActionInfo::Type::kAction}) {
    extensions::Command action;
    if (service->GetExtensionActionCommand(extension.id(), type,
                                           extensions::CommandService::ALL,
                                           &action, nullptr)) {
      action.set_description(
          l10n_util::GetStringUTF16(IDS_EXTENSION_COMMANDS_GENERIC_ACTIVATE));
      add(action);
    }
  }
  ui::CommandMap named;
  service->GetNamedCommands(extension.id(), extensions::CommandService::ALL,
                            extensions::CommandService::ANY_SCOPE, &named);
  for (const auto& [name, command] : named) {
    add(command);
  }
  return commands;
}

extensions::ExtensionId SetShortcut(Profile* profile,
                                    const extensions::ExtensionId& extension,
                                    const std::string& command,
                                    const std::string& shortcut) {
  auto* service = extensions::CommandService::Get(profile);
  RemovedCommands removed(service);
  service->UpdateKeybindingPrefs(extension, command, shortcut);
  // The command's own old key goes first; another command's is the key's.
  for (const auto& [other, name] : removed.list()) {
    if (other != extension || name != command) {
      return other;
    }
  }
  return {};
}

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
