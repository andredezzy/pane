// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_EXTENSIONS_SHORTCUT_REFUSAL_H_
#define PANE_UI_EXTENSIONS_SHORTCUT_REFUSAL_H_

#include <map>
#include <string>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/extensions/commands/command_service.h"
#include "extensions/common/extension_id.h"
#include "ui/base/accelerators/accelerator.h"

class Profile;

namespace pane {

// Pane's menu bar keeps its keys (ADR 0098): an extension's command that
// takes one, on Chrome's shortcuts page or from its manifest's suggested
// key, is refused, and keeps the key it had, or none. Chrome's page checks a
// key only against other extensions' and has no message for this, so it
// shows the command's key again, as Paper 10.3's note asks: "the engine must
// still refuse one, with no message on the page".
class ShortcutRefusal : public extensions::CommandService::Observer {
 public:
  explicit ShortcutRefusal(Profile* profile);
  ShortcutRefusal(const ShortcutRefusal&) = delete;
  ShortcutRefusal& operator=(const ShortcutRefusal&) = delete;
  ~ShortcutRefusal() override;

  // extensions::CommandService::Observer:
  void OnExtensionCommandAdded(const extensions::ExtensionId& extension_id,
                               const std::string& command_name) override;
  void OnExtensionCommandRemoved(const extensions::ExtensionId& extension_id,
                                 const std::string& command_name) override;
  void OnCommandServiceDestroying() override;

 private:
  // An extension's command, by the extension and the command's name.
  using CommandOf = std::pair<extensions::ExtensionId, std::string>;

  // The command's key now, as CommandService holds it.
  ui::Accelerator KeyOf(const CommandOf& command) const;
  // Takes Pane's key off `command`, and gives it back `previous`, unless a
  // later change moved it on already.
  void Refuse(const CommandOf& command, const ui::Accelerator& previous);

  raw_ptr<extensions::CommandService> service_;
  // Each command's key, as the last change left it.
  std::map<CommandOf, ui::Accelerator> keys_;
  // What the change being made took off each command: Chrome takes a
  // command's key off before it gives the new one, in the same task.
  std::map<CommandOf, ui::Accelerator> taken_;
  base::ScopedObservation<extensions::CommandService,
                          extensions::CommandService::Observer>
      observation_{this};
  base::WeakPtrFactory<ShortcutRefusal> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_EXTENSIONS_SHORTCUT_REFUSAL_H_
