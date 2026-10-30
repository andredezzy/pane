// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#import "pane/browser/extensions/extension_menu.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/extensions/context_menu_helpers.h"
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
#include "ui/base/l10n/l10n_util_mac.h"
#include "ui/base/models/image_model.h"
#include "ui/base/models/menu_model.h"
#include "ui/gfx/image/image.h"
#include "url/origin.h"

#import "pane/browser/extensions/extensions_internal.h"

namespace pane {

namespace {

// As Chrome's MenuItemMatchesAction (extension_context_menu_model.cc): the
// items an extension put in its action's context.
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

using extensions::ExtensionContextMenuModel;
using extensions::PermissionsManager;

// As Chrome's CommandIdToSiteAccess: what a page access choice sets.
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

// One of Chrome's page access items, its apostrophes typographic as the
// app's own text ("Can’t Read or Change Site’s Data").
PaneExtensionMenuItem* SiteAccessItem(
    PaneExtensionMenuItemKind kind,
    int command_id,
    std::u16string title,
    bool enabled,
    bool checked = false,
    NSArray<PaneExtensionMenuItem*>* children = @[]) {
  base::ReplaceChars(title, u"'", u"\u2019", &title);
  return [[PaneExtensionMenuItem alloc]
      initWithKind:kind
         commandID:command_id
             title:base::SysUTF16ToNSString(title)
           enabled:enabled
           checked:checked
              icon:nil
          children:children];
}

// The items of `model` that show, with a separator only between two others.
NSArray<PaneExtensionMenuItem*>* ItemsOf(ui::MenuModel& model) {
  NSMutableArray<PaneExtensionMenuItem*>* items = [NSMutableArray array];
  for (size_t i = 0; i < model.GetItemCount(); ++i) {
    if (!model.IsVisibleAt(i)) {
      continue;
    }
    PaneExtensionMenuItemKind kind = PaneExtensionMenuItemKindNormal;
    NSArray<PaneExtensionMenuItem*>* children = @[];
    switch (model.GetTypeAt(i)) {
      case ui::MenuModel::TYPE_SEPARATOR:
        kind = PaneExtensionMenuItemKindSeparator;
        break;
      case ui::MenuModel::TYPE_CHECK:
        kind = PaneExtensionMenuItemKindCheckbox;
        break;
      case ui::MenuModel::TYPE_RADIO:
        kind = PaneExtensionMenuItemKindRadio;
        break;
      case ui::MenuModel::TYPE_SUBMENU:
        kind = PaneExtensionMenuItemKindSubmenu;
        children = ItemsOf(*model.GetSubmenuModelAt(i));
        break;
      default:
        break;
    }
    if (kind == PaneExtensionMenuItemKindSeparator &&
        (!items.count ||
         items.lastObject.kind == PaneExtensionMenuItemKindSeparator)) {
      continue;
    }
    ui::ImageModel icon = model.GetIconAt(i);
    [items addObject:[[PaneExtensionMenuItem alloc]
                         initWithKind:kind
                            commandID:model.GetCommandIdAt(i)
                                // As Chrome's Mac menus show a label:
                                // without its mnemonic marks.
                                title:l10n_util::FixUpWindowsStyleLabel(
                                          model.GetLabelAt(i))
                              enabled:model.IsEnabledAt(i)
                              checked:model.IsItemCheckedAt(i)
                                 icon:icon.IsImage()
                                          ? icon.GetImage().ToNSImage()
                                          : nil
                             children:children]];
  }
  if (items.lastObject.kind == PaneExtensionMenuItemKindSeparator) {
    [items removeLastObject];
  }
  return [items copy];
}

}  // namespace

ExtensionMenu::ExtensionMenu(
    content::BrowserContext* context,
    const content::ContextMenuParams& params,
    base::RepeatingCallback<bool(const extensions::MenuItem*)> filter)
    : params_(params), matcher_(context, this, &model_, std::move(filter)) {}

ExtensionMenu::~ExtensionMenu() = default;

// static
std::unique_ptr<ExtensionMenu> ExtensionMenu::ForPage(
    content::BrowserContext* context,
    const content::ContextMenuParams& params) {
  auto menu = base::WrapUnique(new ExtensionMenu(
      context, params,
      base::BindRepeating(
          &extensions::context_menu_helpers::MenuItemMatchesParams, params)));
  extensions::context_menu_helpers::PopulateExtensionItems(context, params,
                                                           menu->matcher_);
  return menu;
}

// static
std::unique_ptr<ExtensionMenu> ExtensionMenu::ForAction(
    content::BrowserContext* context,
    const extensions::Extension& extension) {
  extensions::ExtensionAction* action =
      extensions::ExtensionActionManager::Get(context)->GetExtensionAction(
          extension);
  std::optional<extensions::ActionInfo::Type> type;
  if (action) {
    type = action->action_type();
  }
  auto menu = base::WrapUnique(
      new ExtensionMenu(context, content::ContextMenuParams(),
                        base::BindRepeating(&MatchesAction, type)));
  int index = 0;
  menu->matcher_.AppendExtensionItems(
      extensions::MenuItem::ExtensionKey(extension.id()), std::u16string(),
      &index, /*is_action_menu=*/true);
  return menu;
}

NSArray<PaneExtensionMenuItem*>* ExtensionMenu::Items() {
  return ItemsOf(model_);
}

void ExtensionMenu::Run(PaneExtensionMenuItem* item,
                        content::WebContents* contents,
                        content::RenderFrameHost* frame) {
  matcher_.ExecuteCommand(item.commandID, contents, frame, params_);
}

bool ExtensionMenu::IsCommandIdChecked(int command_id) const {
  return matcher_.IsCommandIdChecked(command_id);
}

bool ExtensionMenu::IsCommandIdEnabled(int command_id) const {
  return matcher_.IsCommandIdEnabled(command_id);
}

bool ExtensionMenu::IsCommandIdVisible(int command_id) const {
  return matcher_.IsCommandIdVisible(command_id);
}

// The app runs an item through Run, never through the model.
void ExtensionMenu::ExecuteCommand(int command_id, int event_flags) {}

NSArray<PaneExtensionMenuItem*>* SiteAccessItems(
    const extensions::Extension& extension,
    content::WebContents& contents) {
  // As ExtensionContextMenuModel::InitMenu: only for an extension that asks
  // for site access, by host permissions or activeTab.
  PermissionsManager* permissions =
      PermissionsManager::Get(contents.GetBrowserContext());
  const GURL& url = contents.GetLastCommittedURL();
  if (!permissions->CanAffectExtension(extension) &&
      !permissions->HasActiveTabAndCanAccess(extension, url)) {
    return @[];
  }
  // It wants site access, but can't run here.
  if (!permissions->CanUserSelectSiteAccess(
          extension, url, PermissionsManager::UserSiteAccess::kOnClick)) {
    return @[ SiteAccessItem(
        PaneExtensionMenuItemKindNormal,
        ExtensionContextMenuModel::PAGE_ACCESS_CANT_ACCESS,
        l10n_util::GetStringUTF16(IDS_EXTENSIONS_CONTEXT_MENU_CANT_ACCESS_PAGE),
        /*enabled=*/false) ];
  }
  // The three choices, each enabled and checked as the model's delegate
  // says, for the page's origin.
  const GURL site = url::Origin::Create(url).GetURL();
  std::optional<PermissionsManager::UserSiteAccess> current;
  if (!extension.permissions_data()->IsRestrictedUrl(site, nullptr)) {
    current = permissions->GetUserSiteAccess(extension, site);
  }
  auto choice = [&](int command_id, const std::u16string& title) {
    const PermissionsManager::UserSiteAccess access = *SiteAccessOf(command_id);
    return SiteAccessItem(
        PaneExtensionMenuItemKindRadio, command_id, title,
        permissions->CanUserSelectSiteAccess(extension, site, access),
        current == access);
  };
  NSArray<PaneExtensionMenuItem*>* choices = @[
    choice(ExtensionContextMenuModel::PAGE_ACCESS_RUN_ON_CLICK,
           l10n_util::GetStringUTF16(
               IDS_EXTENSIONS_CONTEXT_MENU_PAGE_ACCESS_RUN_ON_CLICK)),
    choice(ExtensionContextMenuModel::PAGE_ACCESS_RUN_ON_SITE,
           l10n_util::GetStringFUTF16(
               IDS_EXTENSIONS_CONTEXT_MENU_PAGE_ACCESS_RUN_ON_SITE,
               url_formatter::IDNToUnicode(
                   url_formatter::StripWWW(url.GetHost())))),
    choice(ExtensionContextMenuModel::PAGE_ACCESS_RUN_ON_ALL_SITES,
           l10n_util::GetStringUTF16(
               IDS_EXTENSIONS_CONTEXT_MENU_PAGE_ACCESS_RUN_ON_ALL_SITES)),
    SiteAccessItem(PaneExtensionMenuItemKindSeparator, -1, std::u16string(),
                   /*enabled=*/true),
    SiteAccessItem(PaneExtensionMenuItemKindNormal,
                   ExtensionContextMenuModel::PAGE_ACCESS_LEARN_MORE,
                   l10n_util::GetStringUTF16(
                       IDS_EXTENSIONS_CONTEXT_MENU_PAGE_ACCESS_LEARN_MORE),
                   /*enabled=*/true),
  ];
  return @[ SiteAccessItem(
      PaneExtensionMenuItemKindSubmenu,
      ExtensionContextMenuModel::PAGE_ACCESS_SUBMENU,
      l10n_util::GetStringUTF16(IDS_EXTENSIONS_CONTEXT_MENU_PAGE_ACCESS),
      /*enabled=*/true, /*checked=*/false, choices) ];
}

void RunSiteAccessItem(PaneExtensionMenuItem* item,
                       const extensions::Extension& extension,
                       content::WebContents& contents,
                       const url::Origin& origin) {
  if (item.commandID == ExtensionContextMenuModel::PAGE_ACCESS_LEARN_MORE) {
    BrowserWindowInterface* browser =
        GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(&contents);
    if (!browser) {
      return;
    }
    NavigateParams params(
        browser,
        GURL(extension_permissions_constants::kRuntimeHostPermissionsHelpURL),
        ui::PAGE_TRANSITION_LINK);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
    return;
  }
  // As Chrome: nothing once the tab has gone to another origin, or for an
  // extension whose site access can't change.
  std::optional<PermissionsManager::UserSiteAccess> access =
      SiteAccessOf(item.commandID);
  if (!access || !origin.IsSameOriginWith(contents.GetLastCommittedURL()) ||
      !PermissionsManager::Get(contents.GetBrowserContext())
           ->CanAffectExtension(extension)) {
    return;
  }
  extensions::SitePermissionsHelper(contents.GetBrowserContext())
      .UpdateSiteAccess(extension, &contents, *access, origin);
}

}  // namespace pane

// MARK: - PaneExtensionMenuItem

@implementation PaneExtensionMenuItem

@synthesize kind = _kind;
@synthesize commandID = _commandID;
@synthesize title = _title;
@synthesize enabled = _enabled;
@synthesize checked = _checked;
@synthesize icon = _icon;
@synthesize children = _children;

- (instancetype)initWithKind:(PaneExtensionMenuItemKind)kind
                   commandID:(int)commandID
                       title:(NSString*)title
                     enabled:(BOOL)enabled
                     checked:(BOOL)checked
                        icon:(NSImage*)icon
                    children:(NSArray<PaneExtensionMenuItem*>*)children {
  if ((self = [super init])) {
    _kind = kind;
    _commandID = commandID;
    _title = [title copy];
    _enabled = enabled;
    _checked = checked;
    _icon = icon;
    _children = [children copy];
  }
  return self;
}

@end

// MARK: - PaneExtensionActionMenu

@implementation PaneExtensionActionMenu {
  std::unique_ptr<pane::ExtensionMenu> _menu;
  base::WeakPtr<content::WebContents> _contents;
  extensions::ExtensionId _extensionID;
  // The page's origin as the menu was made, as Chrome's model keeps it.
  url::Origin _origin;
}

@synthesize items = _items;
@synthesize siteAccessItems = _siteAccessItems;

- (instancetype)initWithMenu:(std::unique_ptr<pane::ExtensionMenu>)menu
                   extension:(const extensions::Extension&)extension
                    contents:(content::WebContents&)contents {
  if ((self = [super init])) {
    _menu = std::move(menu);
    _contents = contents.GetWeakPtr();
    _extensionID = extension.id();
    _origin = url::Origin::Create(contents.GetLastCommittedURL());
    _items = _menu->Items();
    _siteAccessItems = pane::SiteAccessItems(extension, contents);
  }
  return self;
}

// As Chrome's action menu runs one: for the tab, with no frame or page
// clicked; site access for the extension while it is still enabled.
- (void)runItem:(PaneExtensionMenuItem*)item {
  PANE_TRACE("call", "PaneExtensionActionMenu.run",
             base::DictValue().Set("title", base::SysNSStringToUTF8(item.title)));
  if (!_contents) {
    return;
  }
  if (extensions::ContextMenuMatcher::IsExtensionsCustomCommandId(
          item.commandID)) {
    _menu->Run(item, _contents.get(), nullptr);
    return;
  }
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(_contents->GetBrowserContext())
          ->enabled_extensions()
          .GetByID(_extensionID);
  if (extension) {
    pane::RunSiteAccessItem(item, *extension, *_contents, _origin);
  }
}

@end
