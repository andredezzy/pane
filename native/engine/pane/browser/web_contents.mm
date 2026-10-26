// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#import "pane/public/PaneWebContents.h"

#include <string>

#include "base/memory/weak_ptr.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/find_in_page/find_tab_helper.h"
#include "components/find_in_page/find_types.h"
#include "components/zoom/page_zoom.h"
#include "components/zoom/zoom_controller.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/common/page_zoom.h"
#include "net/base/apple/url_conversions.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/ui/tab_bridge.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"

#import "pane/browser/browser_application_internal.h"
#import "pane/browser/web_contents_internal.h"

@implementation PaneWebContents {
  base::WeakPtr<content::WebContents> _contents;
  __weak id<PaneWebContentsDelegate> _delegate;
  // The last text searched, so a repeated search moves to the next match.
  std::u16string _findText;
}

- (instancetype)initWithContents:(content::WebContents*)contents {
  if ((self = [super init])) {
    _contents = contents->GetWeakPtr();
  }
  return self;
}

- (content::WebContents*)contents {
  return _contents.get();
}

- (void)trace:(std::string_view)name args:(base::DictValue)args {
  if (_contents) {
    pane::TabBridge::GetOrCreate(_contents.get())
        ->TraceCallback(name, std::move(args));
  }
}

- (void)traceCall:(std::string_view)name args:(base::DictValue)args {
  if (_contents) {
    args.Set("tab", pane::TabBridge::GetOrCreate(_contents.get())->tab_id());
  }
  pane::Trace("call", name, std::move(args));
}

// MARK: Properties

- (PaneBrowserContext*)context {
  return _contents ? [PaneBrowserApplication.sharedApplication
                         contextForProfile:Profile::FromBrowserContext(
                                               _contents->GetBrowserContext())]
                   : nil;
}

- (id<PaneWebContentsDelegate>)delegate {
  return _delegate;
}

- (void)setDelegate:(id<PaneWebContentsDelegate>)delegate {
  _delegate = delegate;
}

- (NSView*)nativeView {
  return _contents ? _contents->GetNativeView().GetNativeNSView()
                   : [[NSView alloc] init];
}

- (BOOL)isVisible {
  return _contents &&
         _contents->GetVisibility() != content::Visibility::HIDDEN;
}

// The embedder decides visibility, as Chrome's tab strip does for its tabs:
// WebContentsImpl ignores window occlusion until a page was first shown for
// real, so without this a page whose window starts covered (or opens while the
// screen is locked) would stay blank. Chromium also hides the native view of
// a contents created hidden; Chrome's view host unhides it when it attaches a
// tab, so this does too.
- (void)setVisible:(BOOL)visible {
  [self traceCall:"PaneWebContents.setVisible"
             args:base::DictValue().Set("visible", static_cast<bool>(visible))];
  if (!_contents) {
    return;
  }
  _contents->GetNativeView().GetNativeNSView().hidden = !visible;
  if (visible) {
    _contents->WasShown();
  } else {
    _contents->WasHidden();
  }
}

- (NSString*)title {
  return _contents ? base::SysUTF16ToNSString(_contents->GetTitle()) : @"";
}

- (NSURL*)URL {
  return _contents ? net::NSURLWithGURL(_contents->GetVisibleURL()) : nil;
}

- (BOOL)isLoading {
  return _contents && _contents->IsLoading();
}

- (double)loadProgress {
  return _contents ? _contents->GetLoadProgress() : 0;
}

- (NSImage*)favicon {
  auto* driver = _contents ? favicon::ContentFaviconDriver::FromWebContents(
                                 _contents.get())
                           : nullptr;
  if (!driver || !driver->FaviconIsValid()) {
    return nil;
  }
  gfx::Image image = driver->GetFavicon();
  return image.IsEmpty() ? nil : image.ToNSImage();
}

- (BOOL)canGoBack {
  return _contents && _contents->GetController().CanGoBack();
}

- (BOOL)canGoForward {
  return _contents && _contents->GetController().CanGoForward();
}

- (BOOL)isOpen {
  return _contents &&
         GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
             _contents.get());
}

// MARK: Navigation

- (void)loadURL:(NSURL*)URL {
  [self traceCall:"PaneWebContents.loadURL"
             args:base::DictValue().Set(
                      "url", base::SysNSStringToUTF8(URL.absoluteString))];
  if (!_contents) {
    return;
  }
  content::NavigationController::LoadURLParams params(net::GURLWithNSURL(URL));
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  _contents->GetController().LoadURLWithParams(params);
}

- (void)goBack {
  [self traceCall:"PaneWebContents.goBack" args:base::DictValue()];
  if (self.canGoBack) {
    _contents->GetController().GoBack();
  }
}

- (void)goForward {
  [self traceCall:"PaneWebContents.goForward" args:base::DictValue()];
  if (self.canGoForward) {
    _contents->GetController().GoForward();
  }
}

- (void)reload {
  [self traceCall:"PaneWebContents.reload" args:base::DictValue()];
  if (_contents) {
    _contents->GetController().Reload(content::ReloadType::NORMAL, true);
  }
}

- (void)stopLoading {
  [self traceCall:"PaneWebContents.stopLoading" args:base::DictValue()];
  if (_contents) {
    _contents->Stop();
  }
}

// MARK: Find, zoom, focus

- (void)findText:(NSString*)text forward:(BOOL)forward {
  [self traceCall:"PaneWebContents.findText"
             args:base::DictValue()
                      .Set("text", base::SysNSStringToUTF8(text))
                      .Set("forward", static_cast<bool>(forward))];
  auto* finder = _contents ? find_in_page::FindTabHelper::FromWebContents(
                                 _contents.get())
                           : nullptr;
  if (!finder) {
    return;
  }
  pane::TabBridge::GetOrCreate(_contents.get())->Observe();
  std::u16string query = base::SysNSStringToUTF16(text);
  // A new query starts at the first match; the same one moves on.
  const bool find_next = query == _findText;
  _findText = query;
  finder->StartFinding(query, forward, /*case_sensitive=*/false,
                       /*find_match=*/find_next || !query.empty());
}

- (void)stopFinding {
  [self traceCall:"PaneWebContents.stopFinding" args:base::DictValue()];
  _findText.clear();
  if (auto* finder = _contents ? find_in_page::FindTabHelper::FromWebContents(
                                     _contents.get())
                               : nullptr) {
    finder->StopFinding(find_in_page::SelectionAction::kKeep);
  }
}

- (void)zoomIn {
  [self zoom:content::PAGE_ZOOM_IN name:"PaneWebContents.zoomIn"];
}

- (void)zoomOut {
  [self zoom:content::PAGE_ZOOM_OUT name:"PaneWebContents.zoomOut"];
}

- (void)resetZoom {
  [self zoom:content::PAGE_ZOOM_RESET name:"PaneWebContents.resetZoom"];
}

- (void)zoom:(content::PageZoom)zoom name:(std::string_view)name {
  if (!_contents) {
    return;
  }
  auto* controller = zoom::ZoomController::FromWebContents(_contents.get());
  const double before = controller ? controller->GetZoomLevel() : 0;
  zoom::PageZoom::Zoom(_contents.get(), zoom);
  [self traceCall:name
             args:base::DictValue()
                      .Set("before", before)
                      .Set("after", controller ? controller->GetZoomLevel() : 0)
                      .Set("default",
                           controller ? controller->GetDefaultZoomLevel() : 0)
                      .Set("host",
                           content::HostZoomMap::GetZoomLevel(_contents.get()))
                      .Set("temporary",
                           content::HostZoomMap::GetForWebContents(
                               _contents.get())
                               ->UsesTemporaryZoomLevel(
                                   _contents->GetPrimaryMainFrame()
                                       ->GetGlobalId()))];
}

- (void)focus {
  if (_contents) {
    _contents->Focus();
  }
}

- (void)close {
  [self traceCall:"PaneWebContents.close" args:base::DictValue()];
  if (!_contents) {
    return;
  }
  BrowserWindowInterface* browser =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(
          _contents.get());
  if (!browser) {
    return;
  }
  TabStripModel* model = browser->GetTabStripModel();
  const int index = model->GetIndexOfWebContents(_contents.get());
  if (index != TabStripModel::kNoTab) {
    model->CloseWebContentsAt(
        index, TabCloseTypes::CLOSE_USER_GESTURE |
                   TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  }
}

// MARK: From the TabBridge

- (void)contentsDestroyed {
  _contents = nullptr;
}

- (void)bridgeDidChangeTitle {
  NSString* title = self.title;
  [self trace:"PaneWebContentsDelegate.didChangeTitle"
         args:base::DictValue().Set("title", base::SysNSStringToUTF8(title))];
  if ([_delegate respondsToSelector:@selector(webContents:didChangeTitle:)]) {
    [_delegate webContents:self didChangeTitle:title];
  }
}

- (void)bridgeDidChangeURL {
  NSURL* URL = self.URL;
  [self trace:"PaneWebContentsDelegate.didChangeURL"
         args:base::DictValue().Set(
                  "url", base::SysNSStringToUTF8(URL.absoluteString ?: @""))];
  if (URL && [_delegate respondsToSelector:@selector(webContents:
                                                       didChangeURL:)]) {
    [_delegate webContents:self didChangeURL:URL];
  }
}

- (void)bridgeDidChangeNavigationState {
  [self trace:"PaneWebContentsDelegate.didChangeNavigationState"
         args:base::DictValue()
                  .Set("canGoBack", static_cast<bool>(self.canGoBack))
                  .Set("canGoForward", static_cast<bool>(self.canGoForward))];
  if ([_delegate respondsToSelector:@selector
                 (webContentsDidChangeNavigationState:)]) {
    [_delegate webContentsDidChangeNavigationState:self];
  }
}

- (void)bridgeDidChangeLoading:(BOOL)isLoading progress:(double)progress {
  [self trace:"PaneWebContentsDelegate.didChangeLoading"
         args:base::DictValue()
                  .Set("isLoading", static_cast<bool>(isLoading))
                  .Set("progress", progress)];
  if ([_delegate respondsToSelector:@selector(webContents:
                                         didChangeLoading:progress:)]) {
    [_delegate webContents:self didChangeLoading:isLoading progress:progress];
  }
}

- (void)bridgeDidUpdateFavicon:(NSImage*)favicon {
  [self trace:"PaneWebContentsDelegate.didUpdateFavicon" args:base::DictValue()];
  if ([_delegate respondsToSelector:@selector(webContents:
                                         didUpdateFavicon:)]) {
    [_delegate webContents:self didUpdateFavicon:favicon];
  }
}

- (void)bridgeDidFindMatches:(NSInteger)count
                 activeMatch:(NSInteger)activeMatch
                       final:(BOOL)final {
  [self trace:"PaneWebContentsDelegate.didFindMatches"
         args:base::DictValue()
                  .Set("count", static_cast<int>(count))
                  .Set("activeMatch", static_cast<int>(activeMatch))
                  .Set("final", static_cast<bool>(final))];
  if ([_delegate respondsToSelector:@selector
                 (webContents:didFindMatches:activeMatch:final:)]) {
    [_delegate webContents:self
            didFindMatches:count
               activeMatch:activeMatch
                     final:final];
  }
}

- (void)bridgeRendererDidTerminate:(PaneRendererTermination)termination {
  [self trace:"PaneWebContentsDelegate.rendererDidTerminate"
         args:base::DictValue().Set("termination",
                                    static_cast<int>(termination))];
  if ([_delegate respondsToSelector:@selector(webContents:
                                         rendererDidTerminate:)]) {
    [_delegate webContents:self rendererDidTerminate:termination];
  }
}

@end
