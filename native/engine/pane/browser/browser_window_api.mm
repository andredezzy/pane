// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#import "pane/public/PaneBrowserWindow.h"

#include <memory>

#include "base/auto_reset.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "net/base/apple/url_conversions.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/ui/tab_bridge.h"
#include "pane/browser/ui/window_host.h"
#include "ui/gfx/mac/coordinate_conversion.h"
#include "url/gurl.h"

#import "pane/browser/browser_application_internal.h"
#import "pane/browser/browser_context_internal.h"
#import "pane/browser/web_contents_internal.h"

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

- (PaneBrowserContext*)activeContext {
  Profile* profile = _host->active_profile();
  return profile ? [PaneBrowserApplication.sharedApplication
                       contextForProfile:profile]
                 : nil;
}

- (void)setActiveContext:(PaneBrowserContext*)context {
  PANE_TRACE("call", "PaneBrowserWindow.setActiveContext",
             base::DictValue().Set("context", base::SysNSStringToUTF8(
                                                  context.identifier ?: @"")));
  if (Profile* profile = context.profile) {
    _host->BrowserFor(profile);
    _host->SetActiveProfile(profile);
  }
}

- (PaneWebContents*)openTabWithURL:(NSURL*)URL
                         inContext:(PaneBrowserContext*)context
                        foreground:(BOOL)foreground {
  PANE_TRACE("call", "PaneBrowserWindow.openTab",
             base::DictValue()
                 .Set("url", base::SysNSStringToUTF8(URL.absoluteString ?: @""))
                 .Set("context", base::SysNSStringToUTF8(context.identifier))
                 .Set("foreground", static_cast<bool>(foreground)));
  Browser* browser = _host->BrowserFor(context.profile);
  if (!browser) {
    return nil;
  }
  NavigateParams params(browser,
                        URL ? net::GURLWithNSURL(URL) : GURL("about:blank"),
                        ui::PAGE_TRANSITION_TYPED);
  params.disposition = foreground ? WindowOpenDisposition::NEW_FOREGROUND_TAB
                                  : WindowOpenDisposition::NEW_BACKGROUND_TAB;
  params.window_action = NavigateParams::WindowAction::kNoAction;
  base::AutoReset<pane::WindowHost::PendingOpen> pending(
      &_host->pending_open(),
      pane::WindowHost::PendingOpen{params.disposition, nullptr});
  Navigate(&params);
  content::WebContents* contents = params.navigated_or_inserted_contents;
  return contents ? pane::TabBridge::GetOrCreate(contents)->tab() : nil;
}

- (void)activateTab:(PaneWebContents*)tab {
  // Changing the tab strip while it notifies would re-enter TabStripModel.
  if (pane::IsNotifyingTabStrip()) {
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

- (NSArray<PaneWebContents*>*)tabsInContext:(PaneBrowserContext*)context {
  NSMutableArray<PaneWebContents*>* tabs = [NSMutableArray array];
  Browser* browser = _host->ExistingBrowserFor(context.profile);
  if (!browser) {
    return tabs;
  }
  TabStripModel* model = browser->GetTabStripModel();
  for (int i = 0; i < model->count(); ++i) {
    [tabs addObject:pane::TabBridge::GetOrCreate(model->GetWebContentsAt(i))
                        ->tab()];
  }
  return tabs;
}

- (void)close {
  _host->Close();
}

@end
