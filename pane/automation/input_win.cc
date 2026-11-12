// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/automation/input.h"

namespace pane {

// The scenarios' real input on Windows is W6's (docs/plans/views-migration.md):
// none is posted yet.

bool PostKey(views::Widget* widget,
             const std::string& key,
             const std::vector<std::string>& modifiers) {
  return false;
}

void PostClick(views::Widget* widget, int x, int y, int clicks) {}

void PostMove(views::Widget* widget, int x, int y) {}

void PostScroll(views::Widget* widget, int x, int y, double dy) {}

bool SendKeyToWindow(views::Widget* widget,
                     const std::string& key,
                     const std::vector<std::string>& modifiers) {
  return false;
}

void PostModifiers(views::Widget* widget,
                   const std::vector<std::string>& modifiers) {}

}  // namespace pane
