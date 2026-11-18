// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/steady_caret.h"

#include "base/time/time.h"

namespace pane {

// Views' fields and Chrome's pages both blink at the native UI's interval
// (Textfield::ShouldBlinkCursor, renderer_preferences_util.cc): none, no
// blink.
SteadyCaret::SteadyCaret() {
  ui::NativeTheme* theme = ui::NativeTheme::GetInstanceForNativeUi();
  theme->set_caret_blink_interval(base::TimeDelta());
  observation_.Observe(theme);
}

SteadyCaret::~SteadyCaret() = default;

void SteadyCaret::OnNativeThemeUpdated(ui::NativeTheme* observed_theme) {
  observed_theme->set_caret_blink_interval(base::TimeDelta());
}

}  // namespace pane
