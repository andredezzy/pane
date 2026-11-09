// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_VIEW_IDS_H_
#define PANE_UI_VIEW_IDS_H_

#include <string_view>

namespace views {
class View;
}

namespace pane {

// Pane's views by id (View::SetID), one table, as Chrome's view_ids.h keeps
// its own, past Chrome's so a lookup of either never finds the other's.
// Views shows assistive technologies an id as `view_<n>`; the channel reports
// a view by the name the Swift app gave it as an accessibility identifier
// ("pane.content"), where a scenario reads what has the keyboard or what a
// press would reach.
enum ViewId : int {
  kViewIdNone = 0,

  // The sidebar's column, its header (the traffic lights' row), Collapse all
  // there, and the list of profiles and their tabs.
  kViewIdSidebar = 10000,
  kViewIdSidebarHeader,
  kViewIdCollapseAll,
  kViewIdSidebarProfiles,
  // The toolbar's row over the page, its Back, and the address field.
  kViewIdToolbar,
  kViewIdBack,
  kViewIdAddress,
  // The selected tab's page, in its card.
  kViewIdContent,
  // What the content panel shows with no tab selected.
  kViewIdEmpty,
  // The profile sheet, over the window.
  kViewIdProfileSheet,

  kViewIdsEnd,
};

// What a view of `id` goes by ("pane.content"); empty for an id not Pane's.
std::string_view ViewIdName(int id);

// The name of `view`'s id, or of the nearest view around it that has one of
// Pane's; empty where none has.
std::string_view NearestViewIdName(const views::View* view);

}  // namespace pane

#endif  // PANE_UI_VIEW_IDS_H_
