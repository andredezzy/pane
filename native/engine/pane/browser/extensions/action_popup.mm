// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/extensions/action_popup.h"

#include <utility>

#include "base/functional/bind.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/extensions/extension_view_host_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/common/extension.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/ui/browser_window.h"
#include "pane/browser/ui/tab_bridge.h"
#include "pane/browser/ui/window_host.h"

#import "pane/browser/browser_application_internal.h"
#import "pane/browser/extensions/extensions_internal.h"
#import "pane/public/PaneBrowserContext.h"
#import "pane/public/PaneBrowserWindow.h"
#import "pane/public/PaneWebContents.h"

namespace pane {

namespace {

// Chrome's own popup limits (ExtensionPopup).
constexpr gfx::Size kMinSize(25, 25);
constexpr gfx::Size kMaxSize(800, 600);

}  // namespace

// static
void ActionPopup::Show(Browser* browser,
                       const extensions::Extension& extension,
                       ShowPopupCallback callback) {
  extensions::ExtensionAction* action =
      extensions::ExtensionActionManager::Get(browser->GetProfile())
          ->GetExtensionAction(extension);
  content::WebContents* active =
      browser->GetTabStripModel()->GetActiveWebContents();
  const int tab_id =
      active ? sessions::SessionTabHelper::IdForTab(active).id()
             : extensions::ExtensionAction::kDefaultTabId;
  const GURL url = action ? action->GetPopupUrl(tab_id) : GURL();
  if (!url.is_valid()) {
    if (callback) {
      std::move(callback).Run(nullptr);
    }
    return;
  }
  Trace("call", "ExtensionPopup.open",
        base::DictValue().Set("extension", extension.id()));
  std::unique_ptr<extensions::ExtensionViewHost> host =
      extensions::ExtensionViewHostFactory::CreatePopupHost(extension, url,
                                                            browser);
  auto popup = base::WrapUnique(
      new ActionPopup(std::move(host), browser, std::move(callback)));
  ActionPopup* raw = popup.get();
  // The Objective-C popup owns the C++ one and keeps itself until closed.
  PaneExtensionPopup* owner =
      [[PaneExtensionPopup alloc] initWithPopup:std::move(popup)];
  raw->set_owner(owner);
  raw->host_->CreateRendererSoon();
}

ActionPopup::ActionPopup(std::unique_ptr<extensions::ExtensionViewHost> host,
                         Browser* browser,
                         ShowPopupCallback callback)
    : host_(std::move(host)), browser_(browser), callback_(std::move(callback)) {
  host_->set_view(this);
  Observe(host_->host_contents());
  host_->SetCloseHandler(
      base::BindOnce(&ActionPopup::OnHostClosed, weak_factory_.GetWeakPtr()));
}

ActionPopup::~ActionPopup() {
  Observe(nullptr);
  if (callback_) {
    std::move(callback_).Run(nullptr);
  }
  if (host_) {
    host_->set_view(nullptr);
  }
}

content::WebContents* ActionPopup::contents() const {
  return host_ ? host_->host_contents() : nullptr;
}

gfx::NativeView ActionPopup::GetNativeView() {
  return contents() ? contents()->GetNativeView() : gfx::NativeView();
}

void ActionPopup::ResizeDueToAutoResize(content::WebContents* web_contents,
                                        const gfx::Size& new_size) {
  Trace("event", "ExtensionPopup.resize",
        base::DictValue()
            .Set("width", new_size.width())
            .Set("height", new_size.height()));
  size_ = new_size;
  [owner_ popupDidResize:NSMakeSize(new_size.width(), new_size.height())];
}

// The popup's page sizes itself to its content, as in Chrome. Its page
// commits in a new frame (the host's first one is only a placeholder), so
// each frame is set up as it becomes the main one, as views::WebView does.
void ActionPopup::RenderFrameCreated(
    content::RenderFrameHost* render_frame_host) {
  if (contents() && render_frame_host == contents()->GetPrimaryMainFrame()) {
    EnableAutoResize(render_frame_host);
  }
}

void ActionPopup::RenderFrameHostChanged(content::RenderFrameHost* old_host,
                                         content::RenderFrameHost* new_host) {
  if (contents() && new_host == contents()->GetPrimaryMainFrame() &&
      new_host->IsRenderFrameLive()) {
    EnableAutoResize(new_host);
  }
}

void ActionPopup::EnableAutoResize(content::RenderFrameHost* main_frame) {
  if (main_frame->GetView()) {
    main_frame->GetView()->EnableAutoResize(kMinSize, kMaxSize);
  }
}

bool ActionPopup::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  return false;
}

void ActionPopup::OnLoaded() {
  if (shown_ || !contents()) {
    return;
  }
  shown_ = true;
  TabBridge::GetOrCreate(contents())->tab().visible = YES;

  PaneBrowserContext* context = [PaneBrowserApplication.sharedApplication
      contextForProfile:browser_->GetProfile()];
  WindowHost* window_host = HostedBrowserWindow::HostOf(browser_);
  PaneBrowserWindow* window = window_host ? window_host->owner() : nil;
  id<PaneBrowserContextDelegate> delegate = context.delegate;
  Trace("callback", "PaneBrowserContextDelegate.showExtensionPopup",
        base::DictValue().Set("extension", host_->extension_id()));
  if (window && [delegate respondsToSelector:@selector
                         (browserContext:showExtensionPopup:inWindow:)]) {
    [delegate browserContext:context
          showExtensionPopup:owner_
                    inWindow:window];
  }
  if (callback_) {
    std::move(callback_).Run(host_.get());
  }
}

// The popup's page closed itself (window.close(), Escape), or its extension
// went away.
void ActionPopup::OnHostClosed(extensions::ExtensionHost* host) {
  [owner_ popupDidClose];
}

}  // namespace pane
