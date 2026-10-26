// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/extensions/extensions_bridge.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/ui_util.h"
#include "extensions/common/extension.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/ui/window_host.h"
#include "ui/gfx/image/image.h"

#import "pane/browser/extensions/extensions_internal.h"
#import "pane/public/PaneBrowserContext.h"

namespace pane {

ExtensionsBridge::ExtensionsBridge(PaneBrowserContext* context,
                                   Profile* profile)
    : context_(context), profile_(profile) {
  registry_observation_.Observe(extensions::ExtensionRegistry::Get(profile));
  if (auto* dispatcher = extensions::ExtensionActionDispatcher::Get(profile)) {
    action_observation_.Observe(dispatcher);
  }
  if (auto* toolbar = ToolbarActionsModel::Get(profile)) {
    toolbar_observation_.Observe(toolbar);
  }
  tab_strip_tracker_.Init();
}

ExtensionsBridge::~ExtensionsBridge() = default;

int ExtensionsBridge::ActiveTabID() const {
  for (WindowHost* host : WindowHost::All()) {
    Browser* browser = host->ExistingBrowserFor(profile_);
    if (!browser || !host->IsActiveBrowser(browser)) {
      continue;
    }
    if (content::WebContents* contents =
            browser->GetTabStripModel()->GetActiveWebContents()) {
      return sessions::SessionTabHelper::IdForTab(contents).id();
    }
  }
  return extensions::ExtensionAction::kDefaultTabId;
}

extensions::ExtensionActionIconFactory* ExtensionsBridge::IconFactoryFor(
    const extensions::Extension& extension) {
  auto it = icon_factories_.find(extension.id());
  if (it != icon_factories_.end()) {
    return it->second.get();
  }
  extensions::ExtensionAction* action =
      extensions::ExtensionActionManager::Get(profile_)->GetExtensionAction(
          extension);
  if (!action) {
    return nullptr;
  }
  auto factory = std::make_unique<extensions::ExtensionActionIconFactory>(
      &extension, action, this);
  return icon_factories_.emplace(extension.id(), std::move(factory))
      .first->second.get();
}

NSArray* ExtensionsBridge::Snapshot() {
  NSMutableArray<PaneExtension*>* list = [NSMutableArray array];
  if (!profile_) {
    return list;
  }
  const int tab_id = ActiveTabID();
  extensions::ExtensionRegistry* registry =
      extensions::ExtensionRegistry::Get(profile_);
  const extensions::ExtensionSet installed =
      registry->GenerateInstalledExtensionsSet(
          extensions::ExtensionRegistry::ENABLED |
          extensions::ExtensionRegistry::DISABLED);
  for (const auto& extension : installed) {
    if (!extensions::ui_util::ShouldDisplayInExtensionSettings(*extension)) {
      continue;
    }
    NSImage* icon = nil;
    if (registry->enabled_extensions().Contains(extension->id())) {
      if (auto* factory = IconFactoryFor(*extension)) {
        gfx::Image image = factory->GetIcon(tab_id);
        if (!image.IsEmpty()) {
          icon = image.ToNSImage();
        }
      }
    }
    [list addObject:[[PaneExtension alloc] initWithExtension:*extension
                                                     profile:profile_
                                                       tabID:tab_id
                                                        icon:icon]];
  }
  [list sortUsingComparator:^NSComparisonResult(PaneExtension* a,
                                                PaneExtension* b) {
    return [a.name localizedCaseInsensitiveCompare:b.name];
  }];
  return list;
}

void ExtensionsBridge::Changed() {
  if (report_pending_) {
    return;
  }
  report_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ExtensionsBridge::Report,
                                weak_factory_.GetWeakPtr()));
}

void ExtensionsBridge::Report() {
  report_pending_ = false;
  // A factory keeps the default icon it first returned, and the first may
  // have come before the icon's file loaded (a blank placeholder). Once an
  // icon loaded, the factories start over; not from within their own
  // callback (OnIconUpdated), which would destroy the caller.
  if (icons_loaded_) {
    icons_loaded_ = false;
    icon_factories_.clear();
  }
  PaneBrowserContext* context = context_;
  id<PaneBrowserContextDelegate> delegate = context.delegate;
  Trace("callback", "PaneBrowserContextDelegate.didChangeExtensions");
  if ([delegate respondsToSelector:@selector
                (browserContextDidChangeExtensions:)]) {
    [delegate browserContextDidChangeExtensions:context];
  }
}

void ExtensionsBridge::OnExtensionLoaded(content::BrowserContext*,
                                         const extensions::Extension*) {
  Changed();
}

void ExtensionsBridge::OnExtensionUnloaded(
    content::BrowserContext*,
    const extensions::Extension* extension,
    extensions::UnloadedExtensionReason) {
  icon_factories_.erase(extension->id());
  Changed();
}

void ExtensionsBridge::OnExtensionInstalled(content::BrowserContext*,
                                            const extensions::Extension*,
                                            bool) {
  Changed();
}

void ExtensionsBridge::OnExtensionUninstalled(
    content::BrowserContext*,
    const extensions::Extension* extension,
    extensions::UninstallReason) {
  icon_factories_.erase(extension->id());
  Changed();
}

void ExtensionsBridge::OnExtensionActionUpdated(
    extensions::ExtensionAction*,
    content::WebContents*,
    content::BrowserContext* browser_context) {
  if (browser_context == profile_) {
    Changed();
  }
}

void ExtensionsBridge::OnShuttingDown() {
  action_observation_.Reset();
}

void ExtensionsBridge::OnToolbarActionAdded(
    const ToolbarActionsModel::ActionId&) {
  Changed();
}

void ExtensionsBridge::OnToolbarActionRemoved(
    const ToolbarActionsModel::ActionId&) {
  Changed();
}

void ExtensionsBridge::OnToolbarActionUpdated(
    const ToolbarActionsModel::ActionId&) {
  Changed();
}

void ExtensionsBridge::OnToolbarModelInitialized() {
  Changed();
}

void ExtensionsBridge::OnToolbarPinnedActionsChanged() {
  Changed();
}

void ExtensionsBridge::OnToolbarActionsModelShutdown() {
  toolbar_observation_.Reset();
}

void ExtensionsBridge::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    Changed();
  }
}

bool ExtensionsBridge::ShouldTrackBrowser(BrowserWindowInterface* browser) {
  return browser->GetProfile() == profile_;
}

void ExtensionsBridge::OnIconUpdated() {
  icons_loaded_ = true;
  Changed();
}

}  // namespace pane
