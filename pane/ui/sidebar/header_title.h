// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_SIDEBAR_HEADER_TITLE_H_
#define PANE_UI_SIDEBAR_HEADER_TITLE_H_

#include <memory>

#include "base/functional/callback_forward.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/gfx/geometry/rect.h"

namespace views {
class View;
}

namespace pane {

// The sidebar header's title, as each system has it: on the Mac "Pane" after
// the traffic lights, which moves the window as the header does (ADR 0036);
// on Windows "Pane ⌄", the home of the menu bar's commands (Paper 11.4's A),
// a press opening the app's menu under it (11.5). `open_menu` gets the
// title's bounds on the screen and what opened it.
std::unique_ptr<views::View> MakeHeaderTitle(
    base::RepeatingCallback<void(const gfx::Rect& on_screen,
                                 ui::mojom::MenuSourceType source)> open_menu);

// Its bounds in a header whose row is `row` tall, ending before `end`.
gfx::Rect HeaderTitleBounds(views::View& title, int row, int end);

// While the menu it opened shows, its look held as the menu's (Windows').
void SetHeaderMenuShowing(views::View& title, bool showing);

// Opens its menu as a key does (Windows' Alt and F10): whether it opened one.
bool OpenHeaderMenu(views::View& title);

}  // namespace pane

#endif  // PANE_UI_SIDEBAR_HEADER_TITLE_H_
