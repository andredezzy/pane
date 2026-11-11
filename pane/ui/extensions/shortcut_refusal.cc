// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/extensions/shortcut_refusal.h"

#include <vector>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/api/commands/commands_handler.h"
#include "extensions/common/command.h"
#include "extensions/common/extension.h"
#include "pane/browser/engine_trace.h"
#include "pane/ui/app/commands.h"
#include "ui/base/accelerators/command.h"
#include "ui/events/keycodes/keyboard_codes.h"

namespace pane {

namespace {

// One of the keys Pane's menu bar keeps.
bool IsMenuBarKey(const ui::Accelerator& key) {
  return key.key_code() != ui::VKEY_UNKNOWN &&
         CommandForAccelerator(key).has_value();
}

}  // namespace

ShortcutRefusal::ShortcutRefusal(Profile* profile)
    : service_(extensions::CommandService::Get(profile)) {
  if (!service_) {
    return;
  }
  observation_.Observe(service_);
  // The keys the profile's extensions hold already, a key of Pane's among
  // them refused too: one a manifest suggested before this Browser was there.
  for (const auto& extension : extensions::ExtensionRegistry::Get(profile)
                                   ->GenerateInstalledExtensionsSet()) {
    std::vector<std::string> names;
    if (const ui::CommandMap* named =
            extensions::CommandsInfo::GetNamedCommands(extension.get())) {
      for (const auto& [name, command] : *named) {
        names.push_back(name);
      }
    }
    if (const extensions::Command* action =
            extensions::CommandsInfo::GetActionCommand(extension.get())) {
      names.push_back(action->command_name());
    }
    for (const std::string& name : names) {
      const CommandOf command{extension->id(), name};
      const ui::Accelerator key = KeyOf(command);
      if (key.key_code() == ui::VKEY_UNKNOWN) {
        continue;
      }
      keys_[command] = key;
      if (IsMenuBarKey(key)) {
        base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(&ShortcutRefusal::Refuse, weak_factory_.GetWeakPtr(),
                           command, ui::Accelerator()));
      }
    }
  }
}

ShortcutRefusal::~ShortcutRefusal() = default;

void ShortcutRefusal::OnExtensionCommandAdded(
    const extensions::ExtensionId& extension_id,
    const std::string& command_name) {
  const CommandOf command{extension_id, command_name};
  const ui::Accelerator key = KeyOf(command);
  keys_[command] = key;
  if (!IsMenuBarKey(key)) {
    return;
  }
  // Chrome's page (or a manifest) gave the command one of Pane's keys:
  // refused once this change is made, the command given back the key this
  // change took off it.
  auto taken = taken_.find(command);
  ui::Accelerator previous;
  if (taken != taken_.end() && !IsMenuBarKey(taken->second)) {
    previous = taken->second;
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ShortcutRefusal::Refuse,
                                weak_factory_.GetWeakPtr(), command, previous));
}

void ShortcutRefusal::OnExtensionCommandRemoved(
    const extensions::ExtensionId& extension_id,
    const std::string& command_name) {
  const CommandOf command{extension_id, command_name};
  auto key = keys_.find(command);
  if (key == keys_.end()) {
    return;
  }
  // Kept until the change that took it is made.
  if (taken_.empty()) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<ShortcutRefusal> refusal) {
                         if (refusal) {
                           refusal->taken_.clear();
                         }
                       },
                       weak_factory_.GetWeakPtr()));
  }
  taken_[command] = key->second;
  keys_.erase(key);
}

void ShortcutRefusal::OnCommandServiceDestroying() {
  observation_.Reset();
  service_ = nullptr;
}

ui::Accelerator ShortcutRefusal::KeyOf(const CommandOf& command) const {
  return service_->FindCommandByName(command.first, command.second)
      .accelerator();
}

void ShortcutRefusal::Refuse(const CommandOf& command,
                             const ui::Accelerator& previous) {
  // Moved on already, or the service is going.
  if (!service_ || !IsMenuBarKey(KeyOf(command))) {
    return;
  }
  // As the page sets a key, or its Clear none: a key cleared stays so, where
  // RemoveKeybindingPrefs would let Chrome give the manifest's suggested one
  // back.
  const std::string key = previous.key_code() == ui::VKEY_UNKNOWN
                              ? std::string()
                              : ui::Command::AcceleratorToString(previous);
  PANE_TRACE("call", "ExtensionShortcuts.refuse",
             base::DictValue()
                 .Set("extension", command.first)
                 .Set("command", command.second)
                 .Set("previous", key));
  service_->UpdateKeybindingPrefs(command.first, command.second, key);
}

}  // namespace pane
