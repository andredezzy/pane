// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/extensions/installed_extensions.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "base/i18n/string_compare.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/ui_util.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handlers/options_page_info.h"
#include "third_party/icu/source/i18n/unicode/coll.h"

namespace pane {

InstalledExtension::InstalledExtension() = default;
InstalledExtension::InstalledExtension(const InstalledExtension&) = default;
InstalledExtension& InstalledExtension::operator=(const InstalledExtension&) =
    default;
InstalledExtension::~InstalledExtension() = default;

ActionIcons::ActionIcons(base::RepeatingClosure loaded)
    : loaded_callback_(std::move(loaded)) {}

ActionIcons::~ActionIcons() = default;

gfx::Image ActionIcons::IconFor(const extensions::Extension& extension,
                                extensions::ExtensionAction& action,
                                int tab_id) {
  if (std::exchange(loaded_, false)) {
    factories_.clear();
  }
  std::unique_ptr<extensions::ExtensionActionIconFactory>& factory =
      factories_[extension.id()];
  if (!factory) {
    factory = std::make_unique<extensions::ExtensionActionIconFactory>(
        &extension, &action, this);
  }
  return factory->GetIcon(tab_id);
}

void ActionIcons::Forget(const std::string& extension_id) {
  factories_.erase(extension_id);
}

void ActionIcons::OnIconUpdated() {
  loaded_ = true;
  if (loaded_callback_) {
    loaded_callback_.Run();
  }
}

std::vector<InstalledExtension> InstalledExtensions(Profile* profile,
                                                    content::WebContents* tab,
                                                    ActionIcons* icons) {
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
    item.options_url =
        extensions::OptionsPageInfo::GetOptionsPage(extension.get());
    extensions::ExtensionAction* action =
        actions->GetExtensionAction(*extension);
    if (!action) {
      continue;
    }
    item.has_action = true;
    item.action_title = action->GetTitle(tab_id);
    item.action_enabled = action->GetIsVisible(tab_id);
    item.badge_text = action->GetDisplayBadgeText(tab_id);
    if (!item.badge_text.empty()) {
      item.badge_background = action->GetBadgeBackgroundColor(tab_id);
      item.badge_text_color = action->GetBadgeTextColor(tab_id);
    }
    if (item.enabled) {
      // Chrome's toolbar's own choice: the icon the extension set for the
      // tab, else its declarative one, else its default.
      item.icon = icons ? icons->IconFor(*extension, *action, tab_id)
                        : extensions::ExtensionActionIconFactory(
                              extension.get(), action, nullptr)
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
