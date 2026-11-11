// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/extensions/browser_extensions.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "extensions/browser/ui_util.h"
#include "extensions/common/extension.h"
#include "pane/ui/extensions/extensions_ui.h"

namespace pane {

BrowserExtensions::BrowserExtensions(Browser& browser,
                                     base::WeakPtr<ExtensionsUi> ui)
    : browser_(browser),
      ui_(std::move(ui)),
      icons_(base::BindRepeating(&BrowserExtensions::Changed,
                                 base::Unretained(this))),
      shortcuts_(std::make_unique<ExtensionShortcuts>(
          browser.GetProfile(),
          TabListInterface::From(&browser))),
      refusal_(browser.GetProfile()) {
  Profile* profile = browser.GetProfile();
  registry_observation_.Observe(extensions::ExtensionRegistry::Get(profile));
  if (auto* dispatcher = extensions::ExtensionActionDispatcher::Get(profile)) {
    action_observation_.Observe(dispatcher);
  }
  permissions_observation_.Observe(
      extensions::PermissionsManager::Get(profile));
  if (auto* toolbar = ToolbarActionsModel::Get(profile)) {
    toolbar_observation_.Observe(toolbar);
  }
  browser.GetTabStripModel()->AddObserver(this);
  if (ui_) {
    ui_->Register(this);
  }
  // The window shows the profile's extensions once its Browser is there.
  Changed();
}

BrowserExtensions::~BrowserExtensions() {
  if (ui_) {
    ui_->Unregister(this);
  }
}

std::vector<InstalledExtension> BrowserExtensions::List() {
  return InstalledExtensions(browser_->GetProfile(), ActiveTab(), &icons_);
}

content::WebContents* BrowserExtensions::ActiveTab() const {
  return browser_->GetTabStripModel()->GetActiveWebContents();
}

ToolbarActionViewModel* BrowserExtensions::GetActionForId(
    const std::string& action_id) {
  return nullptr;
}

void BrowserExtensions::HideActivePopup() {
  if (ui_) {
    ui_->ClosePopupOf(*browser_);
  }
}

void BrowserExtensions::CloseExtensionsMenuIfOpen() {
  if (ui_) {
    ui_->CloseMenuOf(*browser_);
  }
}

bool BrowserExtensions::ShowToolbarActionPopupForAPICall(
    const std::string& action_id,
    ShowPopupCallback callback) {
  return ui_ && ui_->ShowPopupForAPI(*this, action_id, std::move(callback));
}

void BrowserExtensions::ToggleExtensionsMenu() {
  if (ui_) {
    ui_->ToggleMenuOf(*browser_);
  }
}

bool BrowserExtensions::HasAnyExtensions() const {
  const extensions::ExtensionSet installed =
      extensions::ExtensionRegistry::Get(browser_->GetProfile())
          ->GenerateInstalledExtensionsSet(
              extensions::ExtensionRegistry::ENABLED |
              extensions::ExtensionRegistry::DISABLED);
  for (const auto& extension : installed) {
    if (extensions::ui_util::ShouldDisplayInExtensionSettings(*extension)) {
      return true;
    }
  }
  return false;
}

void BrowserExtensions::Changed() {
  if (report_pending_) {
    return;
  }
  report_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&BrowserExtensions::Report, weak_factory_.GetWeakPtr()));
}

void BrowserExtensions::Report() {
  report_pending_ = false;
  if (ui_) {
    ui_->ExtensionsChanged(*this);
  }
}

void BrowserExtensions::OnExtensionLoaded(content::BrowserContext*,
                                          const extensions::Extension*) {
  Changed();
}

void BrowserExtensions::OnExtensionUnloaded(
    content::BrowserContext*,
    const extensions::Extension* extension,
    extensions::UnloadedExtensionReason) {
  icons_.Forget(extension->id());
  // Its popup goes now, before the extension's host can hear from a page
  // that has none.
  if (ui_) {
    ui_->ExtensionUnloaded(*browser_, extension->id());
  }
  Changed();
}

void BrowserExtensions::OnExtensionInstalled(content::BrowserContext*,
                                             const extensions::Extension*,
                                             bool) {
  Changed();
}

void BrowserExtensions::OnExtensionUninstalled(
    content::BrowserContext*,
    const extensions::Extension* extension,
    extensions::UninstallReason) {
  icons_.Forget(extension->id());
  Changed();
}

void BrowserExtensions::OnExtensionActionUpdated(
    extensions::ExtensionAction*,
    content::WebContents*,
    content::BrowserContext* browser_context) {
  if (browser_context == browser_->GetProfile()) {
    Changed();
  }
}

void BrowserExtensions::OnShuttingDown() {
  action_observation_.Reset();
}

void BrowserExtensions::OnExtensionPermissionsUpdated(
    const extensions::Extension&,
    const extensions::PermissionSet&,
    extensions::PermissionsManager::UpdateReason) {
  Changed();
}

void BrowserExtensions::OnPermissionsManagerShutdown() {
  permissions_observation_.Reset();
}

void BrowserExtensions::OnToolbarActionAdded(
    const ToolbarActionsModel::ActionId&) {
  Changed();
}

void BrowserExtensions::OnToolbarActionRemoved(
    const ToolbarActionsModel::ActionId&) {
  Changed();
}

void BrowserExtensions::OnToolbarActionUpdated(
    const ToolbarActionsModel::ActionId&) {
  Changed();
}

void BrowserExtensions::OnToolbarModelInitialized() {
  Changed();
}

void BrowserExtensions::OnToolbarPinnedActionsChanged() {
  Changed();
}

void BrowserExtensions::OnToolbarActionsModelShutdown() {
  toolbar_observation_.Reset();
}

void BrowserExtensions::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (!selection.active_tab_changed()) {
    return;
  }
  if (ui_ && !tab_strip_model->empty()) {
    ui_->ActiveTabChanged(*browser_);
  }
  Changed();
}

}  // namespace pane
