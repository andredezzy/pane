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
  // The toolbar's row over the page, its Back, Forward, Reload (Stop while
  // the page loads), the address field, its load bar, and the profile's chip.
  kViewIdToolbar,
  kViewIdBack,
  kViewIdForward,
  kViewIdReload,
  kViewIdAddress,
  kViewIdLoadBar,
  kViewIdProfileChip,
  // The selected tab's page, in its card.
  kViewIdContent,
  // What the content panel shows with no tab selected.
  kViewIdEmpty,
  // The profile sheet, over the window.
  kViewIdProfileSheet,
  // The sidebar's rows: a profile's header, a tab, a profile's New tab; and
  // its footer's New and Settings, and its edge, which resizes it. The
  // channel names a profile's and a tab's by their ids
  // ("pane.profile.<id>", "pane.tab.<id>").
  kViewIdProfileRow,
  kViewIdTabRow,
  kViewIdNewTabRow,
  kViewIdSidebarNew,
  kViewIdSidebarSettings,
  kViewIdSidebarResize,
  // The copy of a dragged row that rides the pointer (H3).
  kViewIdDragCopy,
  // The update pill in the sidebar's footer, its button (download, restart,
  // or quit and install), and its dismiss.
  kViewIdUpdatePill,
  kViewIdUpdate,
  kViewIdUpdateDismiss,
  // The sad tab in the page's place, and its Reload.
  kViewIdSadTab,
  kViewIdSadTabReload,
  // The tab switcher over the window; the channel names its rows by their
  // tabs' ids ("pane.switcher.tab.<id>").
  kViewIdSwitcher,
  // Settings in the content panel, and its controls by the Swift app's names:
  // Theme's menu, the App icon, Sleep, Unload, and Trim's menus, the PIN's
  // buttons and Lock after's menu, the update row ("pane.settings.updates",
  // or "pane.settings.upToDate" once a check found nothing newer) and its
  // button, Check for updates or the download's.
  kViewIdSettings,
  kViewIdSettingsTheme,
  kViewIdSettingsAppIcon,
  kViewIdSettingsTabSleep,
  kViewIdSettingsProfileUnload,
  kViewIdSettingsCacheBudget,
  kViewIdSettingsSetUpPin,
  kViewIdSettingsChangePin,
  kViewIdSettingsRemovePin,
  kViewIdSettingsLockAfter,
  kViewIdSettingsUpdates,
  kViewIdSettingsUpToDate,
  kViewIdSettingsCheckUpdates,
  kViewIdSettingsDownloadUpdate,
  // The App icon picker over the window, its Done, and its tiles, which the
  // channel names by their skies ("pane.settings.appIcon.dusk").
  kViewIdAppIconPicker,
  kViewIdAppIconDone,
  kViewIdAppIconTile,
  // Quit Pane? over the window (ADR 0053), its Quit, and its Cancel.
  kViewIdQuit,
  kViewIdQuitConfirm,
  kViewIdQuitCancel,

  kViewIdsEnd,
};

// What a view of `id` goes by ("pane.content"); empty for an id not Pane's.
std::string_view ViewIdName(int id);

// The name of `view`'s id, or of the nearest view around it that has one of
// Pane's; empty where none has.
std::string_view NearestViewIdName(const views::View* view);

}  // namespace pane

#endif  // PANE_UI_VIEW_IDS_H_
