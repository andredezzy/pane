// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_EXTENSIONS_EXTENSION_SHORTCUTS_H_
#define PANE_BROWSER_EXTENSIONS_EXTENSION_SHORTCUTS_H_

#include <string>
#include <vector>

#include "chrome/browser/extensions/extension_keybinding_registry.h"
#include "extensions/common/extension_id.h"
#include "ui/base/accelerators/command.h"

class Profile;
class TabListInterface;

namespace extensions {
class Extension;
}  // namespace extensions

namespace ui {
class Accelerator;
}  // namespace ui

namespace pane {

// The extension's commands, as chrome://extensions/shortcuts lists them
// (ExtensionInfoGenerator's ConstructCommands): its action's, described as
// Chrome's "Activate the extension", then its named ones, each with its
// active shortcut, or none.
std::vector<ui::Command> CommandsOf(Profile* profile,
                                    const extensions::Extension& extension);

// Sets the command's shortcut as chrome://extensions/shortcuts does
// (CommandService::UpdateKeybindingPrefs): `shortcut` as Chrome writes it
// ("Command+Shift+Y"), or empty to clear it. Another command that had the
// key loses it, as in Chrome; its extension is returned, or an empty id.
extensions::ExtensionId SetShortcut(Profile* profile,
                                    const extensions::ExtensionId& extension,
                                    const std::string& command,
                                    const std::string& shortcut);

// The extensions' keyboard shortcuts (chrome.commands) in one Browser, kept
// by Chrome's ExtensionKeybindingRegistry as for a Chrome window: their
// named commands, and their action's (_execute_action), which Chrome's
// toolbar keeps and Pane's app does not.
class ExtensionShortcuts : public extensions::ExtensionKeybindingRegistry {
 public:
  ExtensionShortcuts(Profile* profile, TabListInterface* tabs);
  ExtensionShortcuts(const ExtensionShortcuts&) = delete;
  ExtensionShortcuts& operator=(const ExtensionShortcuts&) = delete;
  ~ExtensionShortcuts() override;

  // The extension and the command `accelerator` is the shortcut of, if any.
  bool Find(const ui::Accelerator& accelerator,
            extensions::ExtensionId* extension,
            std::string* command) const;
  // Sends the command's onCommand, with the active tab.
  bool Notify(const ui::Accelerator& accelerator);

 private:
  // extensions::ExtensionKeybindingRegistry:
  bool PopulateCommands(const extensions::Extension* extension,
                        ui::CommandMap* commands) override;
  bool RegisterAccelerator(const ui::Accelerator& accelerator,
                           const extensions::ExtensionId& extension_id,
                           const std::string& command_name) override;
  bool ShouldIgnoreCommand(const std::string& command) const override;
};

}  // namespace pane

#endif  // PANE_BROWSER_EXTENSIONS_EXTENSION_SHORTCUTS_H_
