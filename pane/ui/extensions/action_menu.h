// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_EXTENSIONS_ACTION_MENU_H_
#define PANE_UI_EXTENSIONS_ACTION_MENU_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/context_menu_params.h"
#include "extensions/common/extension_id.h"
#include "ui/menus/simple_menu_model.h"
#include "url/origin.h"

namespace content {
class WebContents;
}

namespace extensions {
class ContextMenuMatcher;
class Extension;
}  // namespace extensions

namespace pane {

// A pinned action's right-click menu (Paper 04.13 to 04.13.2; ADRs 0054,
// 0096): the system's menu, plain as Aside's, with no icons and in Title Case.
// Its name (the extension's Web Store page); the items the extension put in
// its action's context (chrome.contextMenus), laid out by Chrome's
// ContextMenuMatcher as Chrome's own menu lays them out; its site access on
// the page, as Chrome's menu offers it without kExtensionsMenuAccessControl;
// Remove from Pane and Unpin; then Manage Extension and, for an extension
// that asks for site access, View Web Permissions. A separator stands between
// each group. The menu is made as it is asked for, for the page the action
// shows on, and its items run for that page while it lasts.
class ActionMenu : public ui::SimpleMenuModel::Delegate {
 public:
  // What its own items do, the app's.
  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();

    base::RepeatingClosure store_page;
    base::RepeatingClosure remove;
    base::RepeatingClosure unpin;
    // Manage Extension and View Web Permissions: its Details.
    base::RepeatingClosure manage;
  };

  // `extension`'s menu for `contents`, the page its action shows on.
  ActionMenu(const extensions::Extension& extension,
             content::WebContents& contents,
             Callbacks callbacks);
  ActionMenu(const ActionMenu&) = delete;
  ActionMenu& operator=(const ActionMenu&) = delete;
  ~ActionMenu() override;

  ui::SimpleMenuModel& model() { return model_; }
  const extensions::ExtensionId& extension_id() const { return extension_id_; }
  // Where the extension's own items sit in the model, [begin, end), and the
  // place of its site access, for the channel, which reads them as the
  // Swift's engine handed them over.
  size_t items_begin() const { return items_begin_; }
  size_t items_end() const { return items_end_; }
  std::optional<size_t> site_access_index() const { return site_access_index_; }
  // Chooses the enabled item titled `title`, in a submenu too, as a click in
  // the menu does. Whether there was one.
  bool Choose(std::u16string_view title);

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  bool IsCommandIdVisible(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  // Its site access on the page, as Chrome's menu adds it.
  void AddSiteAccess(const extensions::Extension& extension,
                     content::WebContents& contents);
  // One of the site access items, run as Chrome's model runs it: a choice
  // while the page is still at the origin it was offered for, Learn More in
  // a new tab.
  void RunSiteAccess(int command_id);

  const extensions::ExtensionId extension_id_;
  base::WeakPtr<content::WebContents> contents_;
  // The page's origin as the menu was made, as Chrome's model keeps it.
  const url::Origin origin_;
  const content::ContextMenuParams params_;
  Callbacks callbacks_;
  ui::SimpleMenuModel model_{this};
  std::unique_ptr<extensions::ContextMenuMatcher> matcher_;
  std::unique_ptr<ui::SimpleMenuModel> site_access_;
  // The site access choice that is checked, if one is.
  std::optional<int> checked_access_;
  size_t items_begin_ = 0;
  size_t items_end_ = 0;
  std::optional<size_t> site_access_index_;
};

}  // namespace pane

#endif  // PANE_UI_EXTENSIONS_ACTION_MENU_H_
