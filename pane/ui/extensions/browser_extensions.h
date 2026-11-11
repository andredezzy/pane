// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_EXTENSIONS_BROWSER_EXTENSIONS_H_
#define PANE_UI_EXTENSIONS_BROWSER_EXTENSIONS_H_

#include <string>
#include <vector>

#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/extensions/extension_action_dispatcher.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"
#include "extensions/browser/permissions_manager.h"
#include "pane/browser/ui/window_host.h"
#include "pane/ui/extensions/installed_extensions.h"

class Browser;

namespace pane {

class ExtensionsUi;

// One of the window's Browsers as Chrome's extensions see its toolbar (the
// ExtensionsContainer Chrome registers for its own windows, as
// webui_browser_window.cc does for its WebUI one): chrome.action.openPopup
// shows the popup through it, and what hides a popup or the menu (a media
// picker, Chrome's own menus) asks it, where patch 0005's hook in
// extension_action_api.cc did. It also watches the profile's extensions for
// the window (the Swift's ExtensionsBridge): installs and removals, enabling,
// pins, site access, each action's title, badge, and icon, and the
// Browser's active tab, whose actions show. A burst of changes reports once.
class BrowserExtensions
    : public HostedExtensionsContainer,
      public extensions::ExtensionRegistryObserver,
      public extensions::ExtensionActionDispatcher::Observer,
      public extensions::PermissionsManager::Observer,
      public ToolbarActionsModel::Observer,
      public TabStripModelObserver {
 public:
  // `ui`, the window's extensions, hears of the changes.
  BrowserExtensions(Browser& browser, base::WeakPtr<ExtensionsUi> ui);
  BrowserExtensions(const BrowserExtensions&) = delete;
  BrowserExtensions& operator=(const BrowserExtensions&) = delete;
  // The Browser is going: its popup, if one shows, goes first.
  ~BrowserExtensions() override;

  Browser& browser() const { return *browser_; }
  // What the profile has installed, each action as for the Browser's active
  // tab, its icons from this Browser's factories.
  std::vector<InstalledExtension> List();
  // The Browser's active tab, whose actions show.
  content::WebContents* ActiveTab() const;

  // ExtensionsContainer:
  // Pane's toolbar is its own and keeps no ToolbarActionViewModel: Chrome's
  // callers here are its own toolbar's, the web apps', and the WebUI
  // browser's.
  ToolbarActionViewModel* GetActionForId(const std::string& action_id) override;
  void HideActivePopup() override;
  void CloseExtensionsMenuIfOpen() override;
  bool ShowToolbarActionPopupForAPICall(const std::string& action_id,
                                        ShowPopupCallback callback) override;
  void ToggleExtensionsMenu() override;
  bool HasAnyExtensions() const override;

  // extensions::ExtensionRegistryObserver:
  void OnExtensionLoaded(content::BrowserContext* browser_context,
                         const extensions::Extension* extension) override;
  void OnExtensionUnloaded(content::BrowserContext* browser_context,
                           const extensions::Extension* extension,
                           extensions::UnloadedExtensionReason reason) override;
  void OnExtensionInstalled(content::BrowserContext* browser_context,
                            const extensions::Extension* extension,
                            bool is_update) override;
  void OnExtensionUninstalled(content::BrowserContext* browser_context,
                              const extensions::Extension* extension,
                              extensions::UninstallReason reason) override;

  // extensions::ExtensionActionDispatcher::Observer:
  void OnExtensionActionUpdated(
      extensions::ExtensionAction* extension_action,
      content::WebContents* web_contents,
      content::BrowserContext* browser_context) override;
  void OnShuttingDown() override;

  // extensions::PermissionsManager::Observer:
  void OnExtensionPermissionsUpdated(
      const extensions::Extension& extension,
      const extensions::PermissionSet& permissions,
      extensions::PermissionsManager::UpdateReason reason) override;
  void OnPermissionsManagerShutdown() override;

  // ToolbarActionsModel::Observer:
  void OnToolbarActionAdded(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarActionRemoved(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarActionUpdated(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarModelInitialized() override;
  void OnToolbarPinnedActionsChanged() override;
  void OnToolbarActionsModelShutdown() override;

  // TabStripModelObserver: another tab's actions show as it becomes the
  // active one, and an action's popup closes, as Chrome's does.
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  // Reports a change once the burst it is part of is over.
  void Changed();
  void Report();

  const raw_ref<Browser> browser_;
  const base::WeakPtr<ExtensionsUi> ui_;
  ActionIcons icons_;
  bool report_pending_ = false;
  base::ScopedObservation<extensions::ExtensionRegistry,
                          extensions::ExtensionRegistryObserver>
      registry_observation_{this};
  base::ScopedObservation<extensions::ExtensionActionDispatcher,
                          extensions::ExtensionActionDispatcher::Observer>
      action_observation_{this};
  base::ScopedObservation<extensions::PermissionsManager,
                          extensions::PermissionsManager::Observer>
      permissions_observation_{this};
  base::ScopedObservation<ToolbarActionsModel, ToolbarActionsModel::Observer>
      toolbar_observation_{this};
  base::WeakPtrFactory<BrowserExtensions> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_EXTENSIONS_BROWSER_EXTENSIONS_H_
