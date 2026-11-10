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
    case kViewIdForward:
      return "pane.toolbar.forward";
    case kViewIdReload:
      return "pane.toolbar.reload";
    case kViewIdAddress:
      return "pane.toolbar.address";
    case kViewIdLoadBar:
      return "pane.toolbar.loadBar";
    case kViewIdProfileChip:
      return "pane.toolbar.profile";
    case kViewIdContent:
      return "pane.content";
    case kViewIdEmpty:
      return "pane.empty";
    case kViewIdProfileSheet:
      return "pane.profileSheet";
    case kViewIdProfileRow:
      return "pane.profile";
    case kViewIdTabRow:
      return "pane.tab";
    case kViewIdNewTabRow:
      return "pane.sidebar.newTab";
    case kViewIdSidebarNew:
      return "pane.sidebar.new";
    case kViewIdSidebarSettings:
      return "pane.sidebar.settings";
    case kViewIdSidebarResize:
      return "pane.sidebar.resize";
    case kViewIdDragCopy:
      return "pane.sidebar.dragCopy";
    case kViewIdUpdatePill:
      return "pane.sidebar.updatePill";
    case kViewIdUpdate:
      return "pane.sidebar.update";
    case kViewIdUpdateDismiss:
      return "pane.sidebar.update.dismiss";
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
