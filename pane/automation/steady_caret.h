// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_AUTOMATION_STEADY_CARET_H_
#define PANE_AUTOMATION_STEADY_CARET_H_

#include "base/scoped_observation.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"

namespace pane {

// A scenario's copy draws its carets without blinking, in its fields and its
// pages, as macOS draws them when its user prefers a caret that doesn't
// blink, and Windows when the blink rate is None: a render then shows the
// caret where a field has the keys, in every run, where one caught it on and
// the next off (ADR 0128). The channel holds one while it runs, from before
// any page opens, and keeps it through a change of the system's settings,
// which brings the system's blink back.
class SteadyCaret : public ui::NativeThemeObserver {
 public:
  SteadyCaret();
  SteadyCaret(const SteadyCaret&) = delete;
  SteadyCaret& operator=(const SteadyCaret&) = delete;
  ~SteadyCaret() override;

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

 private:
  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      observation_{this};
};

}  // namespace pane

#endif  // PANE_AUTOMATION_STEADY_CARET_H_
