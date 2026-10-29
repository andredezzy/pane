// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_EXTENSIONS_EXTENSION_MENU_H_
#define PANE_BROWSER_EXTENSIONS_EXTENSION_MENU_H_

#import <Foundation/Foundation.h>

#include <memory>

#include "base/functional/callback.h"
#include "chrome/browser/extensions/context_menu_matcher.h"
#include "content/public/browser/context_menu_params.h"
#include "ui/menus/simple_menu_model.h"

@class PaneExtensionMenuItem;

namespace content {
class BrowserContext;
class RenderFrameHost;
class WebContents;
}  // namespace content

namespace extensions {
class Extension;
class MenuItem;
}  // namespace extensions

namespace pane {

// The items extensions add to a context menu (chrome.contextMenus), laid out
// by Chrome's ContextMenuMatcher as Chrome's own menus lay them out: a page's
// right-click menu, or an action's.
class ExtensionMenu : public ui::SimpleMenuModel::Delegate {
 public:
  // Every extension's items for a right-click with `params`, as Chrome's
  // RenderViewContextMenu.
  static std::unique_ptr<ExtensionMenu> ForPage(
      content::BrowserContext* context,
      const content::ContextMenuParams& params);
  // `extension`'s items in its action's context, as Chrome's
  // ExtensionContextMenuModel: no page was clicked.
  static std::unique_ptr<ExtensionMenu> ForAction(
      content::BrowserContext* context,
      const extensions::Extension& extension);

  ExtensionMenu(const ExtensionMenu&) = delete;
  ExtensionMenu& operator=(const ExtensionMenu&) = delete;
  ~ExtensionMenu() override;

  // The items that show, for the app.
  NSArray<PaneExtensionMenuItem*>* Items();

  // Runs `item` (onClicked in its extension) as clicked in `contents` and
  // `frame`, either of which may be gone.
  void Run(PaneExtensionMenuItem* item,
           content::WebContents* contents,
           content::RenderFrameHost* frame);

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsCommandIdVisible(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  ExtensionMenu(
      content::BrowserContext* context,
      const content::ContextMenuParams& params,
      base::RepeatingCallback<bool(const extensions::MenuItem*)> filter);

  const content::ContextMenuParams params_;
  ui::SimpleMenuModel model_{this};
  extensions::ContextMenuMatcher matcher_;
};

}  // namespace pane

#endif  // PANE_BROWSER_EXTENSIONS_EXTENSION_MENU_H_
