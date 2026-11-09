// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/app/installed_extensions.h"

#include <algorithm>
#include <memory>

#include "base/i18n/string_compare.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_icon_factory.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/ui_util.h"
#include "extensions/common/extension.h"
#include "third_party/icu/source/i18n/unicode/coll.h"

namespace pane {

InstalledExtension::InstalledExtension() = default;
InstalledExtension::InstalledExtension(const InstalledExtension&) = default;
InstalledExtension& InstalledExtension::operator=(const InstalledExtension&) =
    default;
InstalledExtension::~InstalledExtension() = default;

std::vector<InstalledExtension> InstalledExtensions(Profile* profile,
                                                    content::WebContents* tab) {
  std::vector<InstalledExtension> installed;
  if (!profile) {
    return installed;
  }
  const int tab_id = tab ? sessions::SessionTabHelper::IdForTab(tab).id()
                         : extensions::ExtensionAction::kDefaultTabId;
  extensions::ExtensionRegistry* registry =
      extensions::ExtensionRegistry::Get(profile);
  extensions::ExtensionActionManager* actions =
      extensions::ExtensionActionManager::Get(profile);
  const ToolbarActionsModel* toolbar = ToolbarActionsModel::Get(profile);

  for (const auto& extension : registry->GenerateInstalledExtensionsSet(
           extensions::ExtensionRegistry::ENABLED |
           extensions::ExtensionRegistry::DISABLED)) {
    if (!extensions::ui_util::ShouldDisplayInExtensionSettings(*extension)) {
      continue;
    }
    InstalledExtension& item = installed.emplace_back();
    item.id = extension->id();
    item.name = extension->name();
    item.version = extension->VersionString();
    item.enabled = registry->enabled_extensions().Contains(extension->id());
    item.pinned = toolbar && toolbar->IsActionPinned(extension->id());
    extensions::ExtensionAction* action =
        actions->GetExtensionAction(*extension);
    if (!action) {
      continue;
    }
    item.action_title = action->GetTitle(tab_id);
    if (item.enabled) {
      // Chrome's toolbar's own choice: the icon the extension set for the
      // tab, else its declarative one, else its default.
      item.icon = extensions::ExtensionActionIconFactory(extension.get(),
                                                         action, nullptr)
                      .GetIcon(tab_id);
    }
  }

  // By name, case aside, as the locale sorts them (the Swift's
  // localizedCaseInsensitiveCompare).
  UErrorCode status = U_ZERO_ERROR;
  const std::unique_ptr<icu::Collator> collator(
      icu::Collator::createInstance(status));
  if (U_SUCCESS(status)) {
    collator->setStrength(icu::Collator::SECONDARY);
    std::ranges::stable_sort(installed, [&](const InstalledExtension& a,
                                            const InstalledExtension& b) {
      return base::i18n::CompareString16WithCollator(
                 *collator, base::UTF8ToUTF16(a.name),
                 base::UTF8ToUTF16(b.name)) == UCOL_LESS;
    });
  }
  return installed;
}

}  // namespace pane
