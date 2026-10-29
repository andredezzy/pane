// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#import "pane/browser/extensions/extension_menu.h"

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/extensions/context_menu_helpers.h"
#include "chrome/browser/extensions/menu_manager.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/common/api/extension_action/action_info.h"
#include "extensions/common/extension.h"
#include "pane/browser/engine_trace.h"
#include "ui/base/l10n/l10n_util_mac.h"
#include "ui/base/models/image_model.h"
#include "ui/base/models/menu_model.h"
#include "ui/gfx/image/image.h"

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
}

@synthesize items = _items;

- (instancetype)initWithMenu:(std::unique_ptr<pane::ExtensionMenu>)menu
                    contents:(content::WebContents&)contents {
  if ((self = [super init])) {
    _menu = std::move(menu);
    _contents = contents.GetWeakPtr();
    _items = _menu->Items();
  }
  return self;
}

// As Chrome's action menu runs one: for the tab, with no frame or page
// clicked.
- (void)runItem:(PaneExtensionMenuItem*)item {
  PANE_TRACE("call", "PaneExtensionActionMenu.run",
             base::DictValue().Set("title", base::SysNSStringToUTF8(item.title)));
  if (_contents) {
    _menu->Run(item, _contents.get(), nullptr);
  }
}

@end
