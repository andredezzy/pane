// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_APP_APP_DEFAULTS_H_
#define PANE_APP_APP_DEFAULTS_H_

namespace pane {

// The system's defaults Pane registers before the app starts, in memory only
// (PrepareData): Pane restores its own session (profiles.json), so the
// system's window restoration stays off, along with its "reopen windows?"
// alert after a crash; and text without font smoothing, as the Electron app
// drew its UI (CSS -webkit-font-smoothing: antialiased), which Views' text
// doesn't read (ADR 0060).
void RegisterAppDefaults();

}  // namespace pane

#endif  // PANE_APP_APP_DEFAULTS_H_
