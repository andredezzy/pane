// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/extensions/action_menu.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/extensions/context_menu_matcher.h"
#include "chrome/browser/extensions/extension_context_menu_model.h"
#include "chrome/browser/extensions/menu_manager.h"
#include "chrome/browser/extensions/permissions_url_constants.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/grit/generated_resources.h"
#include "components/url_formatter/url_formatter.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/permissions/site_permissions_helper.h"
#include "extensions/browser/permissions_manager.h"
#include "extensions/common/api/extension_action/action_info.h"
#include "extensions/common/extension.h"
#include "extensions/common/permissions/permissions_data.h"
#include "pane/browser/engine_trace.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/text_utils.h"

namespace pane {

namespace {

using extensions::ExtensionContextMenuModel;
using extensions::PermissionsManager;

// As Chrome's MenuItemMatchesAction (extension_context_menu_model.cc), private
// there: the items an extension put in its action's context.
bool MatchesAction(std::optional<extensions::ActionInfo::Type> type,
                   const extensions::MenuItem* item) {
  if (!type) {
    return false;
  }
  using extensions::ActionInfo;
  using extensions::MenuItem;
  const MenuItem::ContextList& contexts = item->contexts();
  return contexts.Contains(MenuItem::ALL) ||
         (contexts.Contains(MenuItem::PAGE_ACTION) &&
          *type == ActionInfo::Type::kPage) ||
         (contexts.Contains(MenuItem::BROWSER_ACTION) &&
          *type == ActionInfo::Type::kBrowser) ||
         (contexts.Contains(MenuItem::ACTION) &&
          *type == ActionInfo::Type::kAction);
}

// As Chrome's CommandIdToSiteAccess: what a site access choice sets.
std::optional<PermissionsManager::UserSiteAccess> SiteAccessOf(int command_id) {
  switch (command_id) {
    case ExtensionContextMenuModel::PAGE_ACCESS_RUN_ON_CLICK:
      return PermissionsManager::UserSiteAccess::kOnClick;
    case ExtensionContextMenuModel::PAGE_ACCESS_RUN_ON_SITE:
      return PermissionsManager::UserSiteAccess::kOnSite;
    case ExtensionContextMenuModel::PAGE_ACCESS_RUN_ON_ALL_SITES:
      return PermissionsManager::UserSiteAccess::kOnAllSites;
    default:
      return std::nullopt;
  }
}

// Chrome's string with its apostrophes typographic, as the app's own text and
// the boards set them ("Can’t Read or Change Site’s Data").
std::u16string Typographic(std::u16string text) {
  base::ReplaceChars(text, u"'", u"’", &text);
  return text;
}

// Runs `closure` once the menu has gone, as a choice that changes what shows
// does (MenuButton's).
void AfterMenu(const base::RepeatingClosure& closure) {
  if (closure) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                             closure);
  }
}

}  // namespace

ActionMenu::Callbacks::Callbacks() = default;
ActionMenu::Callbacks::Callbacks(Callbacks&&) = default;
ActionMenu::Callbacks& ActionMenu::Callbacks::operator=(Callbacks&&) = default;
ActionMenu::Callbacks::~Callbacks() = default;

ActionMenu::ActionMenu(const extensions::Extension& extension,
                       content::WebContents& contents,
                       Callbacks callbacks)
    : extension_id_(extension.id()),
      contents_(contents.GetWeakPtr()),
      origin_(url::Origin::Create(contents.GetLastCommittedURL())),
      callbacks_(std::move(callbacks)) {
  // Its name, its Web Store page.
  model_.AddItem(ExtensionContextMenuModel::HOME_PAGE,
                 base::UTF8ToUTF16(extension.name()));

  // The extension's own items in its action's context, as Chrome's
  // ExtensionContextMenuModel lays them out (no submenu of its name, at most
  // Chrome's six top-level items).
  extensions::ExtensionAction* action =
      extensions::ExtensionActionManager::Get(contents.GetBrowserContext())
          ->GetExtensionAction(extension);
  std::optional<extensions::ActionInfo::Type> type;
  if (action) {
    type = action->action_type();
  }
  matcher_ = std::make_unique<extensions::ContextMenuMatcher>(
      contents.GetBrowserContext(), this, &model_,
      base::BindRepeating(&MatchesAction, type));
  model_.AddSeparator(ui::NORMAL_SEPARATOR);
  items_begin_ = model_.GetItemCount();
  int index = 0;
  matcher_->AppendExtensionItems(
      extensions::MenuItem::ExtensionKey(extension.id()), std::u16string(),
      &index, /*is_action_menu=*/true);
  items_end_ = model_.GetItemCount();
  if (items_end_ == items_begin_) {
    model_.RemoveItemAt(items_begin_ - 1);
    items_begin_ = items_end_ = model_.GetItemCount();
  }

  AddSiteAccess(extension, contents);

  model_.AddSeparator(ui::NORMAL_SEPARATOR);
  model_.AddItem(ExtensionContextMenuModel::UNINSTALL, u"Remove from Pane");
  model_.AddItem(ExtensionContextMenuModel::TOGGLE_VISIBILITY, u"Unpin");
  model_.AddSeparator(ui::NORMAL_SEPARATOR);
  model_.AddItem(ExtensionContextMenuModel::MANAGE_EXTENSIONS,
                 u"Manage Extension");
  if (site_access_index_) {
    model_.AddItem(ExtensionContextMenuModel::VIEW_WEB_PERMISSIONS,
                   u"View Web Permissions");
  }
}

ActionMenu::~ActionMenu() = default;

// As ExtensionContextMenuModel::CreatePageAccessItems on the path without
// kExtensionsMenuAccessControl, the menu Aside shows: only for an extension
// that asks for site access, by host permissions or activeTab.
void ActionMenu::AddSiteAccess(const extensions::Extension& extension,
                               content::WebContents& contents) {
  PermissionsManager* permissions =
      PermissionsManager::Get(contents.GetBrowserContext());
  const GURL& url = contents.GetLastCommittedURL();
  if (!permissions->CanAffectExtension(extension) &&
      !permissions->HasActiveTabAndCanAccess(extension, url)) {
    return;
  }
  model_.AddSeparator(ui::NORMAL_SEPARATOR);
  site_access_index_ = model_.GetItemCount();
  // It wants site access, but can't run here.
  if (!permissions->CanUserSelectSiteAccess(
          extension, url, PermissionsManager::UserSiteAccess::kOnClick)) {
    model_.AddItem(ExtensionContextMenuModel::PAGE_ACCESS_CANT_ACCESS,
                   Typographic(l10n_util::GetStringUTF16(
                       IDS_EXTENSIONS_CONTEXT_MENU_CANT_ACCESS_PAGE)));
    return;
  }
  // The three choices, each enabled and checked as the model's delegate says,
  // for the page's origin; the site is the host without www, as Chrome's
  // GetCurrentSite.
  const GURL site = url::Origin::Create(url).GetURL();
  std::optional<PermissionsManager::UserSiteAccess> current;
  if (!extension.permissions_data()->IsRestrictedUrl(site, nullptr)) {
    current = permissions->GetUserSiteAccess(extension, site);
  }
  site_access_ = std::make_unique<ui::SimpleMenuModel>(this);
  const auto choice = [&](int command_id, const std::u16string& title) {
    const PermissionsManager::UserSiteAccess access = *SiteAccessOf(command_id);
    site_access_->AddRadioItem(command_id, Typographic(title), /*group_id=*/0);
    site_access_->SetEnabledAt(
        site_access_->GetItemCount() - 1,
        permissions->CanUserSelectSiteAccess(extension, site, access));
    if (current == access) {
      checked_access_ = command_id;
    }
  };
  choice(ExtensionContextMenuModel::PAGE_ACCESS_RUN_ON_CLICK,
         l10n_util::GetStringUTF16(
             IDS_EXTENSIONS_CONTEXT_MENU_PAGE_ACCESS_RUN_ON_CLICK));
  choice(
      ExtensionContextMenuModel::PAGE_ACCESS_RUN_ON_SITE,
      l10n_util::GetStringFUTF16(
          IDS_EXTENSIONS_CONTEXT_MENU_PAGE_ACCESS_RUN_ON_SITE,
          url_formatter::IDNToUnicode(url_formatter::StripWWW(url.GetHost()))));
  choice(ExtensionContextMenuModel::PAGE_ACCESS_RUN_ON_ALL_SITES,
         l10n_util::GetStringUTF16(
             IDS_EXTENSIONS_CONTEXT_MENU_PAGE_ACCESS_RUN_ON_ALL_SITES));
  site_access_->AddSeparator(ui::NORMAL_SEPARATOR);
  site_access_->AddItem(
      ExtensionContextMenuModel::PAGE_ACCESS_LEARN_MORE,
      Typographic(l10n_util::GetStringUTF16(
          IDS_EXTENSIONS_CONTEXT_MENU_PAGE_ACCESS_LEARN_MORE)));
  model_.AddSubMenu(ExtensionContextMenuModel::PAGE_ACCESS_SUBMENU,
                    Typographic(l10n_util::GetStringUTF16(
                        IDS_EXTENSIONS_CONTEXT_MENU_PAGE_ACCESS)),
                    site_access_.get());
}

bool ActionMenu::Choose(std::u16string_view title) {
  // The item, in the menu or a submenu, by the title the menu shows.
  ui::MenuModel* found_model = nullptr;
  size_t found_index = 0;
  const auto find = [&](auto& self, ui::MenuModel& model) -> void {
    for (size_t index = 0; index < model.GetItemCount() && !found_model;
         ++index) {
      if (model.GetTypeAt(index) == ui::MenuModel::TYPE_SEPARATOR ||
          !model.IsVisibleAt(index)) {
        continue;
      }
      if (ui::MenuModel* submenu = model.GetSubmenuModelAt(index)) {
        self(self, *submenu);
        continue;
      }
      // In either case: Chromium title-cases its menus' words on the Mac
      // alone (its strings' use_titlecase).
      if (base::EqualsCaseInsensitiveASCII(
              gfx::RemoveAccelerator(model.GetLabelAt(index)), title)) {
        found_model = &model;
        found_index = index;
      }
    }
  };
  find(find, model_);
  if (!found_model || !found_model->IsEnabledAt(found_index)) {
    return false;
  }
  found_model->ActivatedAt(found_index);
  return true;
}

bool ActionMenu::IsCommandIdChecked(int command_id) const {
  if (extensions::ContextMenuMatcher::IsExtensionsCustomCommandId(command_id)) {
    return matcher_->IsCommandIdChecked(command_id);
  }
  return checked_access_ == command_id;
}

bool ActionMenu::IsCommandIdEnabled(int command_id) const {
  if (extensions::ContextMenuMatcher::IsExtensionsCustomCommandId(command_id)) {
    return matcher_->IsCommandIdEnabled(command_id);
  }
  return command_id != ExtensionContextMenuModel::PAGE_ACCESS_CANT_ACCESS;
}

bool ActionMenu::IsCommandIdVisible(int command_id) const {
  if (extensions::ContextMenuMatcher::IsExtensionsCustomCommandId(command_id)) {
    return matcher_->IsCommandIdVisible(command_id);
  }
  return true;
}

// As Chrome's action menu runs one: an extension's item for the tab, with no
// frame or page clicked; a site access choice for the extension while it is
// still enabled; the app's own once the menu has gone.
void ActionMenu::ExecuteCommand(int command_id, int event_flags) {
  PANE_TRACE("call", "ActionMenu.run",
             base::DictValue()
                 .Set("extension", extension_id_)
                 .Set("command", command_id));
  if (extensions::ContextMenuMatcher::IsExtensionsCustomCommandId(command_id)) {
    if (contents_) {
      matcher_->ExecuteCommand(command_id, contents_.get(), nullptr, params_);
    }
    return;
  }
  switch (command_id) {
    case ExtensionContextMenuModel::HOME_PAGE:
      AfterMenu(callbacks_.store_page);
      return;
    case ExtensionContextMenuModel::UNINSTALL:
      AfterMenu(callbacks_.remove);
      return;
    case ExtensionContextMenuModel::TOGGLE_VISIBILITY:
      AfterMenu(callbacks_.unpin);
      return;
    case ExtensionContextMenuModel::MANAGE_EXTENSIONS:
    case ExtensionContextMenuModel::VIEW_WEB_PERMISSIONS:
      AfterMenu(callbacks_.manage);
      return;
    default:
      RunSiteAccess(command_id);
      return;
  }
}

void ActionMenu::RunSiteAccess(int command_id) {
  if (!contents_) {
    return;
  }
  if (command_id == ExtensionContextMenuModel::PAGE_ACCESS_LEARN_MORE) {
    BrowserWindowInterface* browser =
        GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
            contents_.get());
    if (!browser) {
      return;
    }
    NavigateParams params(
        browser,
        GURL(extension_permissions_constants::kRuntimeHostPermissionsHelpURL),
        ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::kNewForegroundTab;
    Navigate(&params);
    return;
  }
  // As Chrome: nothing once the tab has gone to another origin, or for an
  // extension whose site access can't change.
  const std::optional<PermissionsManager::UserSiteAccess> access =
      SiteAccessOf(command_id);
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(contents_->GetBrowserContext())
          ->enabled_extensions()
          .GetByID(extension_id_);
  if (!access || !extension ||
      !origin_.IsSameOriginWith(contents_->GetLastCommittedURL()) ||
      !PermissionsManager::Get(contents_->GetBrowserContext())
           ->CanAffectExtension(*extension)) {
    return;
  }
  extensions::SitePermissionsHelper(contents_->GetBrowserContext())
      .UpdateSiteAccess(*extension, contents_.get(), *access, origin_);
}

}  // namespace pane
