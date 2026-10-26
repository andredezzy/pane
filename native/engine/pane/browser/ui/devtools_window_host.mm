// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/ui/devtools_window_host.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/memory/weak_ptr.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/ui/browser_window.h"
#include "pane/browser/ui/tab_bridge.h"
#include "pane/browser/ui/window_host.h"

#import "pane/public/PaneBrowserWindow.h"
#import "pane/public/PaneWebContents.h"

// Shows the DevTools Browser's page filling its window, and lets the window
// close with it.
@interface HostedDevToolsWindowController : NSObject <PaneBrowserWindowDelegate>
- (instancetype)initWithWindow:(PaneBrowserWindow*)window;
- (void)showPage:(nullable PaneWebContents*)page;
- (void)showWindow;
@end

@implementation HostedDevToolsWindowController {
  PaneBrowserWindow* __strong _window;
  PaneWebContents* __strong _page;
  // The controller keeps itself (and the window) until the window closes.
  HostedDevToolsWindowController* __strong _keepAlive;
}

- (instancetype)initWithWindow:(PaneBrowserWindow*)window {
  if ((self = [super init])) {
    _window = window;
    _keepAlive = self;
    window.delegate = self;
  }
  return self;
}

- (void)showPage:(PaneWebContents*)page {
  if (page == _page) {
    return;
  }
  [_page.nativeView removeFromSuperview];
  _page = page;
  if (!page) {
    return;
  }
  NSView* host = _window.hostView;
  NSView* view = page.nativeView;
  view.frame = host.bounds;
  view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [host addSubview:view];
  page.visible = YES;
}

- (void)showWindow {
  NSWindow* window = _window.window;
  if (!window.visible) {
    [window center];
  }
  [window makeKeyAndOrderFront:nil];
}

- (void)browserWindow:(PaneBrowserWindow*)window
         didInsertTab:(PaneWebContents*)tab
               opener:(PaneWebContents*)opener
           foreground:(BOOL)foreground {
  [self showPage:tab];
}

- (void)browserWindow:(PaneBrowserWindow*)window
       didActivateTab:(PaneWebContents*)tab {
  [self showPage:tab];
}

- (void)browserWindow:(PaneBrowserWindow*)window
         didRemoveTab:(PaneWebContents*)tab {
  if (tab == _page) {
    [self showPage:nil];
  }
}

- (BOOL)browserWindowShouldClose:(PaneBrowserWindow*)window {
  return YES;
}

- (void)browserWindowWillClose:(PaneBrowserWindow*)window {
  [self showPage:nil];
  _keepAlive = nil;
}

@end

namespace pane {

BrowserWindow* CreateDevToolsWindow(Browser* browser) {
  Trace("call", "DevToolsWindow.undock");
  PaneBrowserWindow* window =
      [[PaneBrowserWindow alloc] initWithContentRect:NSMakeRect(0, 0, 960, 640)];
  window.window.identifier = @"pane.devtools";
  window.window.title = @"DevTools";
  HostedDevToolsWindowController* controller =
      [[HostedDevToolsWindowController alloc] initWithWindow:window];

  WindowHost* host = WindowHost::ForNSWindow(window.window);
  auto* hosted = new HostedBrowserWindow(host->GetWeakPtr());
  hosted->AttachBrowser(browser);

  // The Browser is still being built: it joins the window a task later.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<WindowHost> host,
             base::WeakPtr<BrowserWindowInterface> alive, Browser* built,
             HostedDevToolsWindowController* controller) {
            if (!host || !alive) {
              return;
            }
            host->AdoptBrowser(built, /*close_when_empty=*/true);
            if (content::WebContents* page =
                    built->GetTabStripModel()->GetActiveWebContents()) {
              [controller showPage:TabBridge::GetOrCreate(page)->tab()];
            }
            [controller showWindow];
          },
          host->GetWeakPtr(), browser->GetWeakPtr(), browser, controller));
  return hosted;
}

}  // namespace pane
