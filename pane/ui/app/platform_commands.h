// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_APP_PLATFORM_COMMANDS_H_
#define PANE_UI_APP_PLATFORM_COMMANDS_H_

#include "pane/ui/app/commands.h"

namespace views {
class Widget;
}

namespace pane {

// The platform's own commands (IsPlatformCommand), which Pane runs itself
// where the system has no menu bar to run them: from Windows' app menu
// (Paper 11.5), the Edit menu on what has the keys, Minimize and Maximize on
// `widget`, and About Pane over it (11.15 A). On the Mac AppKit runs them
// through the responder chain (menu_bar_mac.mm), and none reaches here.
void RunPlatformCommand(views::Widget* widget, Command command);

}  // namespace pane

#endif  // PANE_UI_APP_PLATFORM_COMMANDS_H_
