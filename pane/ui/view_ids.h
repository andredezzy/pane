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
  // A page's dialog over the window (ADR 0021), as the Swift named its
  // surface, and its fields and buttons, which the channel names by their
  // places ("pane.dialog.field.0", "pane.dialog.button.1": the default
  // button is the first).
  kViewIdDialog,
  kViewIdDialogField,
  kViewIdDialogButton,
  // Find in page's bar (ADR 0067), as the Swift named it, its field and
  // count, and its chevrons and close.
  kViewIdFind,
  kViewIdFindField,
  kViewIdFindCount,
  kViewIdFindPrevious,
  kViewIdFindNext,
  kViewIdFindClose,
  // DevTools docked beside the page (ADR 0024).
  kViewIdDevTools,
  // The full screen's Esc hint (ADR 0025).
  kViewIdExclusiveAccessHint,
  // The toolbar's extensions (ADR 0054): the pinned actions' capsule, an
  // action, which the channel names by its extension ("pane.extension.<id>"),
  // and the puzzle, by the Swift's names.
  kViewIdExtensionActions,
  kViewIdExtensionAction,
  kViewIdExtensions,
  // The extensions menu under the puzzle, its rows, and an extension's pin
  // and more, named by the extension ("pane.extensionsMenu.pin.<id>"); the
  // more menu beside it and its rows.
  kViewIdExtensionsMenu,
  kViewIdExtensionsMenuManage,
  kViewIdExtensionsMenuWebStore,
  kViewIdExtensionsMenuExtension,
  kViewIdExtensionsMenuPin,
  kViewIdExtensionsMenuMore,
  kViewIdExtensionMoreMenu,
  kViewIdExtensionMoreMenuStorePage,
  kViewIdExtensionMoreMenuRemove,
  kViewIdExtensionMoreMenuPin,
  kViewIdExtensionMoreMenuManage,
  kViewIdExtensionMoreMenuOptions,
  // An action's popup, its bubble in a window of its own (ADR 0031), and
  // the extension's page in it. The Swift named none of them; its
  // "pane.extensionPopup" is the prompt that removes an extension.
  kViewIdActionPopup,
  kViewIdActionPopupPage,
  // Adding or removing an extension, as each asks (ADR 0056), by the Swift's
  // names: Dia's dialog ("pane.extensionInstall"), Aside's popup
  // ("pane.extensionPopup"), either one's accept and Cancel, and the popup's
  // Report abuse.
  kViewIdExtensionInstall,
  kViewIdExtensionPopup,
  kViewIdExtensionPopupAccept,
  kViewIdExtensionPopupCancel,
  kViewIdExtensionPopupReportAbuse,
  // The profile sheet's controls, by the Swift's names where it gave them
  // ("pane.profile.name", "pane.profile.save", "pane.profile.platform",
  // "pane.profile.timeZone", "pane.profile.proxyHost",
  // "pane.profile.proxyPort", "pane.profile.testProxy",
  // "pane.profile.proxyStatus"), and in their form where it gave none.
  kViewIdProfileClose,
  kViewIdProfileBody,
  kViewIdProfileName,
  kViewIdProfileColor,
  kViewIdProfileSwatch,
  kViewIdProfileHistory,
  kViewIdProfileSpoof,
  kViewIdProfilePlatform,
  kViewIdProfilePlatformTile,
  kViewIdProfileWarning,
  kViewIdProfileTimeZone,
  kViewIdProfileProxy,
  kViewIdProfileProxyType,
  kViewIdProfileProxyHost,
  kViewIdProfileProxyPort,
  kViewIdProfileProxyUsername,
  kViewIdProfileProxyPassword,
  kViewIdProfileTestProxy,
  kViewIdProfileProxyStatus,
  kViewIdProfileCancel,
  kViewIdProfileSave,

  kViewIdsEnd,
};

// What a view of `id` goes by ("pane.content"); empty for an id not Pane's.
std::string_view ViewIdName(int id);

// The name of `view`'s id, or of the nearest view around it that has one of
// Pane's; empty where none has.
std::string_view NearestViewIdName(const views::View* view);

}  // namespace pane

#endif  // PANE_UI_VIEW_IDS_H_
