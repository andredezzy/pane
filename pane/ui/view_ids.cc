// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/view_ids.h"

#include "ui/views/view.h"

namespace pane {

std::string_view ViewIdName(int id) {
  switch (id) {
    case kViewIdSidebar:
      return "pane.sidebar";
    case kViewIdSidebarHeader:
      return "pane.sidebar.header";
    case kViewIdCollapseAll:
      return "pane.sidebar.collapseAll";
    case kViewIdSidebarProfiles:
      return "pane.sidebar.profiles";
    case kViewIdToolbar:
      return "pane.toolbar";
    case kViewIdBack:
      return "pane.toolbar.back";
    case kViewIdAddress:
      return "pane.toolbar.address";
    case kViewIdContent:
      return "pane.content";
    case kViewIdEmpty:
      return "pane.empty";
    case kViewIdProfileSheet:
      return "pane.profileSheet";
    default:
      return std::string_view();
  }
}

std::string_view NearestViewIdName(const views::View* view) {
  for (; view; view = view->parent()) {
    if (const std::string_view name = ViewIdName(view->GetID());
        !name.empty()) {
      return name;
    }
  }
  return std::string_view();
}

}  // namespace pane
