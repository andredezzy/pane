// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_EXTENSIONS_EXTENSIONS_BRIDGE_H_
#define PANE_BROWSER_EXTENSIONS_EXTENSIONS_BRIDGE_H_

#include <map>
#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/extensions/extension_action_dispatcher.h"
#include "chrome/browser/ui/browser_tab_strip_tracker.h"
#include "chrome/browser/ui/browser_tab_strip_tracker_delegate.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "extensions/browser/extension_action_icon_factory.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"

@class NSArray;
@class PaneBrowserContext;
@class PaneExtension;

class Profile;

namespace pane {

// Watches a profile's extensions (installs, enabling, pins, and each action's
// title, badge, and icon, which differ per tab) and the profile's selected
// tab, and tells the PaneBrowserContext's delegate, coalescing bursts into one
// report.
class ExtensionsBridge : public extensions::ExtensionRegistryObserver,
                         public extensions::ExtensionActionDispatcher::Observer,
                         public ToolbarActionsModel::Observer,
                         public extensions::ExtensionActionIconFactory::Observer,
                         public TabStripModelObserver,
                         public BrowserTabStripTrackerDelegate {
 public:
  ExtensionsBridge(PaneBrowserContext* context, Profile* profile);
  ExtensionsBridge(const ExtensionsBridge&) = delete;
  ExtensionsBridge& operator=(const ExtensionsBridge&) = delete;
  ~ExtensionsBridge() override;

  // Every extension the user installed, as now, actions for the profile's
  // active tab.
  NSArray* Snapshot();

  // The profile's active tab, or ExtensionAction::kDefaultTabId.
  int ActiveTabID() const;

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

  // ToolbarActionsModel::Observer:
  void OnToolbarActionAdded(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarActionRemoved(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarActionUpdated(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarModelInitialized() override;
  void OnToolbarPinnedActionsChanged() override;
  void OnToolbarActionsModelShutdown() override;

  // extensions::ExtensionActionIconFactory::Observer:
  void OnIconUpdated() override;

  // TabStripModelObserver: another tab's actions show as it is selected.
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // BrowserTabStripTrackerDelegate:
  bool ShouldTrackBrowser(BrowserWindowInterface* browser) override;

 private:
  // Reports a change once the current burst is over.
  void Changed();
  void Report();
  extensions::ExtensionActionIconFactory* IconFactoryFor(
      const extensions::Extension& extension);

  __weak PaneBrowserContext* context_;
  raw_ptr<Profile> profile_;
  std::map<std::string, std::unique_ptr<extensions::ExtensionActionIconFactory>>
      icon_factories_;
  bool report_pending_ = false;
  bool icons_loaded_ = false;
  base::ScopedObservation<extensions::ExtensionRegistry,
                          extensions::ExtensionRegistryObserver>
      registry_observation_{this};
  base::ScopedObservation<extensions::ExtensionActionDispatcher,
                          extensions::ExtensionActionDispatcher::Observer>
      action_observation_{this};
  base::ScopedObservation<ToolbarActionsModel, ToolbarActionsModel::Observer>
      toolbar_observation_{this};
  BrowserTabStripTracker tab_strip_tracker_{this, this};
  base::WeakPtrFactory<ExtensionsBridge> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_BROWSER_EXTENSIONS_EXTENSIONS_BRIDGE_H_
