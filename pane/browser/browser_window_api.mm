// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include <memory>

#include "base/auto_reset.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"  // nogncheck
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "net/base/apple/url_conversions.h"
#include "pane/browser/app_callback.h"
#import "pane/browser/browser_application_internal.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/ui/tab_bridge.h"
#include "pane/browser/ui/window_host.h"
#import "pane/browser/web_contents_internal.h"
#import "pane/public/PaneBrowserWindow.h"
#include "ui/gfx/mac/coordinate_conversion.h"
#include "url/gurl.h"

@implementation PaneBrowserWindow {
  std::unique_ptr<pane::WindowHost> _host;
  __weak id<PaneBrowserWindowDelegate> _delegate;
}

- (instancetype)initWithContentRect:(NSRect)rect {
  if ((self = [super init])) {
    PANE_TRACE("call", "PaneBrowserWindow.init");
    _host = std::make_unique<pane::WindowHost>(self,
                                               gfx::ScreenRectFromNSRect(rect));
  }
  return self;
}

- (NSWindow*)window {
  return _host->ns_window();
}

- (NSView*)hostView {
  return _host->host_view();
}

- (id<PaneBrowserWindowDelegate>)delegate {
  return _delegate;
}

- (void)setDelegate:(id<PaneBrowserWindowDelegate>)delegate {
  _delegate = delegate;
}

- (void)activateTab:(PaneWebContents*)tab {
  // Changing the tab strip while it notifies would re-enter TabStripModel.
  if (pane::InAppCallback()) {
    dispatch_async(dispatch_get_main_queue(), ^{
      [self activateTab:tab];
    });
    return;
  }
  content::WebContents* contents = tab.contents;
  if (!contents) {
    return;
  }
  PANE_TRACE_TAB(pane::TabBridge::GetOrCreate(contents),
                 "PaneBrowserWindow.activateTab");
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(contents);
  if (!browser) {
    return;
  }
  TabStripModel* model = browser->GetTabStripModel();
  const int index = model->GetIndexOfWebContents(contents);
  if (index != TabStripModel::kNoTab && index != model->active_index()) {
    model->ActivateTabAt(index);
  }
}

- (void)close {
  // Closing runs beforeunload and closes Browsers: never from inside a tab
  // strip notification or a tab's callback.
  if (pane::InAppCallback()) {
    dispatch_async(dispatch_get_main_queue(), ^{
      [self close];
    });
    return;
  }
  _host->Close();
}

@end
