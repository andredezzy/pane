// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_WINDOW_FOCUS_GROUPS_H_
#define PANE_UI_WINDOW_FOCUS_GROUPS_H_

#include <memory>
#include <vector>

#include "ui/base/accelerators/accelerator.h"

namespace pane {

class RootView;

// The keys between the window's groups on Windows, as Chrome's between its
// panes, the user's call (2026-11-14, ADR 0019): F6 and Shift+F6 move them
// along the address field, the toolbar, the sidebar, and the page, around;
// Alt+Shift+T to the toolbar's first button; in the toolbar or the sidebar,
// whose buttons take no Tab (tab_order.h), the arrows, Home, and End move
// between its buttons, and Esc gives the keys back. On the Mac none of it,
// as before.
class FocusGroups {
 public:
  // None on the Mac.
  static std::unique_ptr<FocusGroups> Make(RootView& root);
  // The keys the window hears for it: none on the Mac.
  static std::vector<ui::Accelerator> Keys();

  virtual ~FocusGroups() = default;

  // One of Keys(): true when it moved the keys.
  virtual bool Move(const ui::Accelerator& key) = 0;
  // The window goes: the keys a group took go with it.
  virtual void Detach() = 0;
};

}  // namespace pane

#endif  // PANE_UI_WINDOW_FOCUS_GROUPS_H_
