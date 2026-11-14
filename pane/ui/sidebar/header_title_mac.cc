// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sidebar/header_title.h"

#include <algorithm>

#include "base/functional/callback.h"
#include "pane/ui/design/palette.h"
#include "pane/ui/design/text.h"
#include "ui/views/controls/label.h"

namespace pane {

namespace {

// 82 in, after the traffic lights, 12 semibold (ADR 0036).
constexpr int kLeading = 82;
constexpr int kTrailing = 8;
constexpr TextStyle kTitle{12, weight::kSemibold, 0};

}  // namespace

// The menu bar's commands are the menu bar's: the title opens nothing.
std::unique_ptr<views::View> MakeHeaderTitle(
    base::RepeatingCallback<void(const gfx::Rect&, ui::mojom::MenuSourceType)>
        open_menu) {
  std::unique_ptr<views::Label> title =
      MakeLabel(u"Pane", kTitle, kPaneColorMutedForeground);
  // The title moves the window, as the rest of the header does.
  title->SetCanProcessEventsWithinSubtree(false);
  title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return title;
}

gfx::Rect HeaderTitleBounds(views::View& title, int row, int end) {
  return gfx::Rect(kLeading, 0, std::max(end - kTrailing - kLeading, 0), row);
}

void SetHeaderMenuShowing(views::View& title, bool showing) {}

bool OpenHeaderMenu(views::View& title) {
  return false;
}

}  // namespace pane
