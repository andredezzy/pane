// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/system_alert.h"

#include <utility>

#include "base/functional/callback.h"
#include "pane/ui/window/root_view.h"

namespace pane {

// Pane's own dialog over the window, as Exit Pane? is drawn (Paper 11.13 A,
// the user's call, 2026-11-14): Windows' TaskDialog has no dark look, and
// would stand apart from the window that asks. The window's root draws it
// (RootView::ShowAlert); a window without one hears that the action was not
// chosen.
void ShowSystemAlert(views::Widget* parent,
                     const SystemAlert& alert,
                     base::OnceCallback<void(bool chosen)> done) {
  RootView* root = RootView::Of(parent);
  if (!root) {
    std::move(done).Run(false);
    return;
  }
  root->ShowAlert(alert, std::move(done));
}

}  // namespace pane
