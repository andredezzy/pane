// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_EXTENSIONS_EXTENSION_SHORTCUTS_H_
#define PANE_BROWSER_EXTENSIONS_EXTENSION_SHORTCUTS_H_

#include <string>

#include "chrome/browser/extensions/extension_keybinding_registry.h"
#include "extensions/common/extension_id.h"

class Profile;
class TabListInterface;

namespace ui {
class Accelerator;
}  // namespace ui

namespace pane {

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
