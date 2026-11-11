// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_EXTENSIONS_EXTENSIONS_UI_H_
#define PANE_UI_EXTENSIONS_EXTENSIONS_UI_H_

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/ui/extensions/extension_popup_types.h"
#include "pane/ui/extensions/installed_extensions.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"

class Browser;
class Profile;

namespace extensions {
class Extension;
}

namespace views {
class MenuRunner;
}

namespace pane {

class ActionMenu;
class ActionPopup;
class BrowserExtensions;
class HostedExtensionsContainer;
class Window;

// The window's extensions (the Swift's ExtensionsController, its toolbar part,
// and ExtensionPopover): the shown profile's pinned actions and the puzzle in
// the toolbar (ADR 0054), the extensions menu under the puzzle and an
// extension's more menu beside it, a pinned action's right-click menu
// (ADR 0096), and an action's popup (ADR 0031), each per profile: a profile's
// extensions, pins, and actions are its own Chrome profile's. Every press
// asks the app first (InteractionAllowed), as the Swift's did. Esc closes the
// more menu, then the menu; so do a click outside them, another tab,
// profile, or page, the window giving up the keys, and the Lock.
class ExtensionsUi {
 public:
  // The app behind the window.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // The window, which shows the toolbar and the menus.
    virtual Window& ExtensionsWindow() = 0;
    // Whether the user may act (the app's InteractionAllowed).
    virtual bool ExtensionsAllowed() const = 0;
    // The profile the window shows, open in the engine: its toolbar shows
    // its extensions. None off the browser page (Settings, the Extensions
    // page), where the toolbar goes.
    virtual Profile* ShownProfile() const = 0;
    // A new tab of `profile`'s, selected, at `url`: Visit Chrome Web Store,
    // an extension's Web Store page, its options.
    virtual void OpenExtensionTab(Profile& profile, const GURL& url) = 0;
    // Manage all extensions, and an extension's Manage Extension and View
    // Web Permissions (its Details, `extension_id`): Pane's Extensions page,
    // the Swift's target, until X4 opens chrome://extensions in the
    // profile's tab.
    virtual void ShowExtensionsPage(Profile& profile,
                                    const std::string& extension_id) = 0;
    // Remove from Pane asks first, in the popup ADR 0056 draws (X3's).
    virtual void ConfirmUninstall(Profile& profile,
                                  const std::string& extension_id) = 0;
  };

  explicit ExtensionsUi(Delegate& delegate);
  ExtensionsUi(const ExtensionsUi&) = delete;
  ExtensionsUi& operator=(const ExtensionsUi&) = delete;
  ~ExtensionsUi();

  // The container of `browser`, one of the window's Browsers
  // (WindowHost::Delegate::CreateExtensionsContainer).
  std::unique_ptr<HostedExtensionsContainer> MakeContainer(Browser& browser);

  // What shows: the menu, the extension whose more menu shows, and the
  // popup, while it shows or loads.
  bool menu_open() const { return menu_open_; }
  const std::optional<std::string>& more_menu() const { return more_menu_; }
  ActionPopup* popup() const { return popup_.get(); }
  // The shown profile's extensions, as the toolbar and the menu show them.
  const std::vector<InstalledExtension>& shown() const { return shown_; }

  // The toolbar's presses: the puzzle; a pinned action, which runs it, or
  // closes its popup; and a pinned action's right-click at `point` (in the
  // screen), its menu.
  void PressPuzzle();
  void PressAction(const std::string& extension_id);
  void ShowActionMenu(const std::string& extension_id,
                      const gfx::Point& point,
                      ui::mojom::MenuSourceType source);
  // The menu's: Manage all extensions, Visit Chrome Web Store, a row (its
  // extension's action), its pin, and its more.
  void PressManage();
  void PressWebStore();
  void PressRow(const std::string& extension_id);
  void PressPin(const std::string& extension_id);
  void PressMore(const std::string& extension_id);
  // The more menu's: the extension's Web Store page, Remove from Pane, Pin
  // or Unpin, Manage Extension, and Options.
  void PressStorePage(const std::string& extension_id);
  void PressRemove(const std::string& extension_id);
  void PressMorePin(const std::string& extension_id);
  void PressManageExtension(const std::string& extension_id);
  void PressOptions(const std::string& extension_id);
  // The menu closes, and the more menu with it: a click outside, another
  // tab, profile, or page, the window giving up the keys.
  void CloseMenu();
  // Esc, while the menu shows: the more menu, then the menu. Whether it
  // took the key.
  bool Escape();

  // The channel's: an action run as its button runs it; the popup closed;
  // the pin of an extension of `profile`'s (pinned in its toolbar model);
  // and the right-click menu of a shown extension's action, made as a
  // right-click makes it and kept until the next, to read and choose in
  // (null when the extension shows no action).
  void RunAction(const std::string& extension_id);
  void ClosePopup();
  void SetPinned(Profile& profile, const std::string& extension_id, bool pin);
  ActionMenu* MakeActionMenu(const std::string& extension_id);

  // The window shows another tab, profile, or page, or what it shows
  // changed: the toolbar's actions and the menu show the shown profile's,
  // as for its tab.
  void Refresh();
  // The Lock: the menus and the popup go at once.
  void Lock();

  // From BrowserExtensions.
  void Register(BrowserExtensions* container);
  void Unregister(BrowserExtensions* container);
  void ExtensionsChanged(BrowserExtensions& container);
  void ExtensionUnloaded(Browser& browser, const std::string& extension_id);
  void ActiveTabChanged(Browser& browser);
  bool ShowPopupForAPI(BrowserExtensions& container,
                       const std::string& extension_id,
                       ShowPopupCallback callback);
  void ClosePopupOf(Browser& browser);
  void CloseMenuOf(Browser& browser);
  void ToggleMenuOf(Browser& browser);

  base::WeakPtr<ExtensionsUi> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

 private:
  // The container of the Browser the window shows, the window's active one.
  BrowserExtensions* Shown() const;
  // A shown, enabled extension.
  const extensions::Extension* EnabledExtension(
      const std::string& extension_id) const;
  // The menu opens, or closes.
  void SetMenuOpen(bool open);
  // The shown profile's extensions again, into the toolbar and the menu.
  void ShowState();
  // Shows the popup of `extension`, in `container`'s Browser, in place of
  // the one showing, pointing at its pinned button, else the puzzle.
  void ShowPopup(BrowserExtensions& container,
                 const extensions::Extension& extension,
                 std::optional<uint64_t> interval,
                 ShowPopupCallback callback);
  // The popup goes: at once (its Browser or its extension went, the Lock),
  // or fading out as AppKit's popover went.
  void ClosePopupNow(bool animate);
  // The popup closed itself: its page's window.close() or Esc, a click
  // outside it.
  void PopupClosed(ActionPopup* popup, bool by_activation);
  // A closing popup has faded out.
  void PopupGone(ActionPopup* popup);
  // Where a popup of `extension_id` points, in the screen: its pinned
  // button, else the puzzle, else the window's top right.
  gfx::Rect PopupAnchor(const std::string& extension_id) const;
  // The pinned action's right-click menu went.
  void ActionMenuGone();
  void Trace(std::string_view name, const std::string& extension_id) const;

  const raw_ref<Delegate> delegate_;
  std::set<BrowserExtensions*> containers_;
  // The shown profile's extensions, as last read.
  std::vector<InstalledExtension> shown_;
  bool menu_open_ = false;
  std::optional<std::string> more_menu_;
  std::unique_ptr<ActionPopup> popup_;
  // Popups fading out, and the extension whose popup a click outside closed
  // last, and when: a press on its own button then closes it, rather than
  // opening it again.
  std::vector<std::unique_ptr<ActionPopup>> closing_;
  std::optional<std::pair<std::string, base::TimeTicks>> closed_by_click_;
  // A pinned action's right-click menu, the extension it is for, and the one
  // the channel made last.
  std::unique_ptr<ActionMenu> action_menu_;
  std::unique_ptr<views::MenuRunner> action_menu_runner_;
  std::optional<std::string> action_menu_extension_;
  base::WeakPtrFactory<ExtensionsUi> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_EXTENSIONS_EXTENSIONS_UI_H_
