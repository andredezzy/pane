// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_DIALOGS_SYSTEM_ALERT_H_
#define PANE_UI_DIALOGS_SYSTEM_ALERT_H_

#include <string>

#include "base/functional/callback_forward.h"

namespace views {
class Widget;
}

namespace pane {

// What the system's alert asks: a title, a message under it, the action's
// button, which is destructive, and Cancel.
struct SystemAlert {
  std::u16string title;
  std::u16string message;
  std::u16string action;
  std::u16string cancel;
};

// Asks before something that can't be undone as the system asks (Paper 03.1's
// "drawn by macOS"; the Swift's SwiftUI alert): on the Mac an alert sheet on
// `parent`'s window, the action's button marked destructive, Cancel taking
// Esc. `done` says whether the action was chosen; it runs once the sheet has
// gone.
void ShowSystemAlert(views::Widget* parent,
                     const SystemAlert& alert,
                     base::OnceCallback<void(bool chosen)> done);

}  // namespace pane

#endif  // PANE_UI_DIALOGS_SYSTEM_ALERT_H_
