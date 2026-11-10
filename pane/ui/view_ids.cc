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
    case kViewIdSadTab:
      return "pane.sadTab";
    case kViewIdSadTabReload:
      return "pane.sadTab.reload";
    case kViewIdSwitcher:
      return "pane.switcher";
    case kViewIdSettings:
      return "pane.settings";
    case kViewIdSettingsTheme:
      return "pane.settings.theme";
    case kViewIdSettingsAppIcon:
      return "pane.settings.appIcon";
    case kViewIdSettingsTabSleep:
      return "pane.settings.tabSleep";
    case kViewIdSettingsProfileUnload:
      return "pane.settings.profileUnload";
    case kViewIdSettingsCacheBudget:
      return "pane.settings.cacheBudget";
    case kViewIdSettingsSetUpPin:
      return "pane.settings.setUpPIN";
    case kViewIdSettingsChangePin:
      return "pane.settings.changePIN";
    case kViewIdSettingsRemovePin:
      return "pane.settings.removePIN";
    case kViewIdSettingsLockAfter:
      return "pane.settings.lockAfter";
    case kViewIdSettingsUpdates:
      return "pane.settings.updates";
    case kViewIdSettingsUpToDate:
      return "pane.settings.upToDate";
    case kViewIdSettingsCheckUpdates:
      return "pane.settings.checkUpdates";
    case kViewIdSettingsDownloadUpdate:
      return "pane.settings.downloadUpdate";
    case kViewIdAppIconPicker:
      return "pane.appIcon";
    case kViewIdAppIconDone:
      return "pane.appIcon.done";
    case kViewIdAppIconTile:
      return "pane.appIcon.tile";
    case kViewIdQuit:
      return "pane.quit";
    case kViewIdQuitConfirm:
      return "pane.quit.confirm";
    case kViewIdQuitCancel:
      return "pane.quit.cancel";
    case kViewIdDialog:
      return "pane.dialog";
    case kViewIdDialogField:
      return "pane.dialog.field";
    case kViewIdDialogButton:
      return "pane.dialog.button";
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
