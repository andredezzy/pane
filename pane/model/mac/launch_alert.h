// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_MODEL_MAC_LAUNCH_ALERT_H_
#define PANE_MODEL_MAC_LAUNCH_ALERT_H_

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"

namespace pane::model {

// What the launch asks or tells the user before the engine runs: the Electron
// migration's (RunLaunchMigration). Nothing may touch NSApplication before
// Chromium creates it, so the system's alert is a CFUserNotification, which
// the system draws for the process.
class LaunchAlert {
 public:
  enum class Response {
    kDefaultButton,
    kAlternateButton,
    // `dismiss_when` held first.
    kDismissed,
  };

  // Shows until it goes, with the work it names.
  class Note {
   public:
    virtual ~Note() = default;
  };

  // The system's alerts. `silent`, for a scenario's run (PANE_AUTOMATION_DIR),
  // none shows: scenarios run on a Mac in use, where the system draws these in
  // front of every app and gives them the keyboard, so the user's next Return
  // pressed one (the migration scenario's Cancel quit Pane). An alert then
  // waits as for a user who presses nothing: until `dismiss_when` holds, or
  // not at all without one. So does one the system would not show.
  static std::unique_ptr<LaunchAlert> Create(bool silent);

  virtual ~LaunchAlert() = default;

  // Waits for a button, or for `dismiss_when`, if any, to hold (asked every
  // second), which takes the alert down by itself.
  virtual Response Show(const std::string& title,
                        const std::string& message,
                        const std::string& default_button,
                        const std::optional<std::string>& alternate_button,
                        base::RepeatingCallback<bool()> dismiss_when) = 0;

  // A note without buttons, or null when none shows.
  virtual std::unique_ptr<Note> ShowNote(const std::string& title,
                                         const std::string& message) = 0;
};

}  // namespace pane::model

#endif  // PANE_MODEL_MAC_LAUNCH_ALERT_H_
