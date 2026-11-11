// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_UI_EXTENSIONS_ACTION_POPUP_H_
#define PANE_UI_EXTENSIONS_ACTION_POPUP_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "chrome/browser/ui/extensions/extension_popup_types.h"
#include "pane/ui/design/motion.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/widget/widget_observer.h"

class Browser;
class GURL;

namespace extensions {
class Extension;
class ExtensionHost;
class ExtensionViewHost;
}  // namespace extensions

namespace views {
class Widget;
class WidgetDelegate;
}  // namespace views

namespace pane {

// The popup's window's name, by which the channel tells it from Chrome's.
inline constexpr char kActionPopupWindowName[] = "PaneActionPopup";

// An action's popup (ADR 0031): the extension's page in Pane's bubble, as the
// Swift's NSPopover showed it and Paper 04.12 draws it, which the user kept
// over Chrome's square ExtensionPopup on Paper 10.5. A window of its own over
// Pane's, as NSPopover's was: the system's popover material (white, or #2C2C2E
// on dark, at 96%) rounded 14 as macOS rounds a popover's corner (ADR 0057),
// a hairline, the window server's shadow, an arrow from its top edge whose
// tip touches the toolbar button it points at, its middle under it, and the
// page in a 6-point frame of the material, on white, its corners rounded 9.
// The page sizes itself within Chrome's limits, 25 × 25 to 800 × 600, cut to
// the room the screen leaves under the button; the bubble keeps on the
// screen, the arrow at the button. Chrome's own popup host (an
// ExtensionViewHost) runs the page; the bubble shows once its first load has
// finished, as Chrome's does, and fades in and out as AppKit's popover did,
// at once under Reduce Motion. A click outside it, or the window giving up
// the keys to another app, closes it, as NSPopover's transient behavior did,
// unless DevTools inspect its page or the page shows a dialog; so does Esc in
// its page, and the page's own window.close().
class ActionPopup : public views::WidgetObserver {
 public:
  struct Callbacks {
    Callbacks();
    Callbacks(Callbacks&&);
    Callbacks& operator=(Callbacks&&);
    ~Callbacks();

    // It closed itself: its page asked (window.close(), Esc), or a click
    // outside it (`by_click`). Its owner closes it.
    base::RepeatingCallback<void(ActionPopup*, bool by_click)> closed;
    // It has faded out, and can go.
    base::RepeatingCallback<void(ActionPopup*)> gone;
  };

  // The popup of `extension`'s action at `url`, for `browser`'s active tab,
  // pointing at `anchor` (in the screen) over `parent`; shown once its page
  // has loaded, taking the keys when `activate`. `interval` is the press
  // that asked for it, ended by its first frame on the screen; `shown`
  // (chrome.action.openPopup's) hears of it once it shows, or of nothing if
  // it never does.
  ActionPopup(Browser& browser,
              const extensions::Extension& extension,
              const GURL& url,
              views::Widget& parent,
              const gfx::Rect& anchor,
              bool activate,
              std::optional<uint64_t> interval,
              ShowPopupCallback shown,
              Callbacks callbacks);
  ActionPopup(const ActionPopup&) = delete;
  ActionPopup& operator=(const ActionPopup&) = delete;
  ~ActionPopup() override;

  const std::string& extension_id() const { return extension_id_; }
  Browser* browser() const { return browser_; }
  // Its page has loaded, and it shows.
  bool shown() const { return shown_; }
  // The page's size, as it sized itself (the Swift's preferredSize).
  gfx::Size page_size() const;
  views::Widget* widget() const { return widget_.get(); }
  // Fades out, then tells its owner it can go (`gone`); hidden at once
  // where it never showed.
  void FadeOut();

  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  class Bubble;
  class Page;

  // From the page: its first load finished; its size changed; it closed.
  void PageLoaded();
  void PageResized();
  void HostClosed(extensions::ExtensionHost* host);
  // Places the window under the anchor, its arrow at the anchor's middle.
  void Place();
  // Whether something keeps it open as it loses the keys: DevTools on its
  // page, a dialog of its page's.
  bool KeptOpen() const;
  // Tells the owner it closed itself, on the next turn: the host may be in
  // the middle of its own close.
  void ClosedItself(bool by_click);

  const std::string extension_id_;
  raw_ptr<Browser> browser_;
  const raw_ptr<views::Widget> parent_;
  const gfx::Rect anchor_;
  const bool activate_;
  // The biggest page the screen has room for under the anchor.
  gfx::Size max_page_;
  std::optional<uint64_t> interval_;
  ShowPopupCallback shown_callback_;
  Callbacks callbacks_;
  std::unique_ptr<extensions::ExtensionViewHost> host_;
  std::unique_ptr<views::WidgetDelegate> delegate_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<Bubble> bubble_ = nullptr;
  raw_ptr<Page> page_ = nullptr;
  bool shown_ = false;
  bool closing_ = false;
  // The window's opacity as it fades in and out.
  ValueMotion fade_;
  base::ScopedMultiSourceObservation<views::Widget, views::WidgetObserver>
      widget_observations_{this};
  base::WeakPtrFactory<ActionPopup> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_UI_EXTENSIONS_ACTION_POPUP_H_
