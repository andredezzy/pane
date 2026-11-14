// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/dialogs/system_alert.h"

#include <utility>

#include "base/functional/callback.h"

namespace pane {

// The system's alert on Windows waits for its board in Paper (ADR 0058),
// which W5's boards don't draw (docs/plans/views-migration.md): none shows
// yet, and the action is not chosen.
void ShowSystemAlert(views::Widget* parent,
                     const SystemAlert& alert,
                     base::OnceCallback<void(bool chosen)> done) {
  std::move(done).Run(false);
}

}  // namespace pane
