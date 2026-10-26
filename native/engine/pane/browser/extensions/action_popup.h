// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_EXTENSIONS_ACTION_POPUP_H_
#define PANE_BROWSER_EXTENSIONS_ACTION_POPUP_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/extensions/extension_view.h"
#include "chrome/browser/ui/extensions/extension_popup_types.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/gfx/geometry/size.h"

@class PaneExtensionPopup;

class Browser;

namespace extensions {
class Extension;
class ExtensionHost;
class ExtensionViewHost;
}  // namespace extensions

namespace pane {

// An extension action's popup page in one of Pane's windows: Chrome's popup
// host (an ExtensionViewHost) with Pane in place of Chrome's bubble. Its page
// sizes itself to its content, within Chrome's popup limits. The app hears of
// it once the page loaded (showExtensionPopup:inWindow:), and shows its view.
class ActionPopup : public extensions::ExtensionView,
                    public content::WebContentsObserver {
 public:
  // Opens `extension`'s popup for `browser`'s active tab. `callback`
  // (chrome.action.openPopup) runs once it shows, or with null if it can't.
  static void Show(Browser* browser,
                   const extensions::Extension& extension,
                   ShowPopupCallback callback);

  ActionPopup(const ActionPopup&) = delete;
  ActionPopup& operator=(const ActionPopup&) = delete;
  ~ActionPopup() override;

  content::WebContents* contents() const;
  const gfx::Size& size() const { return size_; }
  void set_owner(PaneExtensionPopup* owner) { owner_ = owner; }

  // extensions::ExtensionView:
  gfx::NativeView GetNativeView() override;
  void ResizeDueToAutoResize(content::WebContents* web_contents,
                             const gfx::Size& new_size) override;
  bool HandleKeyboardEvent(content::WebContents* source,
                           const input::NativeWebKeyboardEvent& event) override;
  void OnLoaded() override;

  // extensions::ExtensionView and content::WebContentsObserver:
  void RenderFrameCreated(content::RenderFrameHost* render_frame_host) override;

  // content::WebContentsObserver:
  void RenderFrameHostChanged(content::RenderFrameHost* old_host,
                              content::RenderFrameHost* new_host) override;

 private:
  ActionPopup(std::unique_ptr<extensions::ExtensionViewHost> host,
              Browser* browser,
              ShowPopupCallback callback);

  void OnHostClosed(extensions::ExtensionHost* host);
  void EnableAutoResize(content::RenderFrameHost* main_frame);

  std::unique_ptr<extensions::ExtensionViewHost> host_;
  raw_ptr<Browser> browser_;
  ShowPopupCallback callback_;
  gfx::Size size_;
  bool shown_ = false;
  __weak PaneExtensionPopup* owner_;
  base::WeakPtrFactory<ActionPopup> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_BROWSER_EXTENSIONS_ACTION_POPUP_H_
