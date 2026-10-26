// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#import "pane/public/PaneWebContents.h"

#include <memory>

#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/favicon/core/favicon_driver_observer.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/visibility.h"
#include "net/base/apple/url_conversions.h"
#include "pane/browser/engine_trace.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"

#import "pane/browser/browser_context_internal.h"
#import "pane/browser/web_contents_internal.h"

@interface PaneWebContents ()
- (void)hostDidChangeTitle;
- (void)hostDidChangeURL;
- (void)hostDidChangeLoading:(BOOL)isLoading progress:(double)progress;
- (void)hostDidUpdateFavicon:(NSImage*)favicon;
@end

namespace pane {

// Owns one WebContents for a PaneWebContents and turns Chromium's observer
// callbacks into the Objective-C delegate's.
class WebContentsHost : public content::WebContentsDelegate,
                        public content::WebContentsObserver,
                        public favicon::FaviconDriverObserver {
 public:
  WebContentsHost(PaneWebContents* owner, Profile* profile) : owner_(owner) {
    // Created hidden, like a background tab; the app shows the page it
    // selects (see -setVisible:).
    content::WebContents::CreateParams params(profile);
    params.initially_hidden = true;
    contents_ = content::WebContents::Create(params);
    contents_->SetDelegate(this);
    Observe(contents_.get());

    favicon::CreateContentFaviconDriverForWebContents(contents_.get());
    favicon::ContentFaviconDriver::FromWebContents(contents_.get())
        ->AddObserver(this);
  }

  WebContentsHost(const WebContentsHost&) = delete;
  WebContentsHost& operator=(const WebContentsHost&) = delete;

  ~WebContentsHost() override {
    favicon::ContentFaviconDriver::FromWebContents(contents_.get())
        ->RemoveObserver(this);
    Observe(nullptr);
    contents_->SetDelegate(nullptr);
  }

  content::WebContents* contents() const { return contents_.get(); }

  // content::WebContentsObserver:
  void TitleWasSet(content::NavigationEntry* entry) override {
    [owner_ hostDidChangeTitle];
  }

  void PrimaryPageChanged(content::Page& page) override {
    [owner_ hostDidChangeURL];
  }

  void DidStartLoading() override {
    [owner_ hostDidChangeLoading:YES progress:0];
  }

  void DidStopLoading() override {
    [owner_ hostDidChangeLoading:NO progress:1];
  }

  void LoadProgressChanged(double progress) override {
    [owner_ hostDidChangeLoading:contents_->IsLoading() progress:progress];
  }

  // favicon::FaviconDriverObserver:
  void OnFaviconUpdated(favicon::FaviconDriver* favicon_driver,
                        NotificationIconType notification_icon_type,
                        const GURL& icon_url,
                        bool icon_url_changed,
                        const gfx::Image& image) override {
    if (notification_icon_type != NON_TOUCH_16_DIP || image.IsEmpty()) {
      return;
    }
    [owner_ hostDidUpdateFavicon:image.ToNSImage()];
  }

 private:
  __weak PaneWebContents* owner_;
  std::unique_ptr<content::WebContents> contents_;
};

}  // namespace pane

namespace {

NSString* TraceID(PaneWebContents* webContents) {
  return [NSString stringWithFormat:@"%p", webContents];
}

}  // namespace

@implementation PaneWebContents {
  PaneBrowserContext* _context;
  std::unique_ptr<pane::WebContentsHost> _host;
  __weak id<PaneWebContentsDelegate> _delegate;
}

- (instancetype)initWithContext:(PaneBrowserContext*)context {
  if ((self = [super init])) {
    _context = context;
    _host = std::make_unique<pane::WebContentsHost>(self, context.profile);
    NSView* view = _host->contents()->GetNativeView().GetNativeNSView();
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  }
  return self;
}

- (void)dealloc {
  _host.reset();
}

- (PaneBrowserContext*)context {
  return _context;
}

- (id<PaneWebContentsDelegate>)delegate {
  return _delegate;
}

- (void)setDelegate:(id<PaneWebContentsDelegate>)delegate {
  _delegate = delegate;
}

- (NSView*)nativeView {
  return _host ? _host->contents()->GetNativeView().GetNativeNSView()
               : [[NSView alloc] init];
}

- (NSString*)title {
  return _host ? base::SysUTF16ToNSString(_host->contents()->GetTitle()) : @"";
}

- (NSURL*)URL {
  return _host ? net::NSURLWithGURL(_host->contents()->GetVisibleURL()) : nil;
}

- (BOOL)isLoading {
  return _host && _host->contents()->IsLoading();
}

- (BOOL)isVisible {
  return _host && _host->contents()->GetVisibility() !=
                      content::Visibility::HIDDEN;
}

// The embedder decides visibility, as Chrome's tab strip does for its tabs:
// WebContentsImpl ignores window occlusion until a page was first shown for
// real, so without this a page whose window starts covered (or opens while the
// screen is locked) would stay blank. Chromium also hides the native view of
// a contents created hidden; Chrome's view host unhides it when it attaches a
// tab, so this does too.
- (void)setVisible:(BOOL)visible {
  pane::Trace("call", "PaneWebContents.setVisible",
              base::DictValue()
                  .Set("webContents", base::SysNSStringToUTF8(TraceID(self)))
                  .Set("visible", static_cast<bool>(visible)));
  if (!_host) {
    return;
  }
  _host->contents()->GetNativeView().GetNativeNSView().hidden = !visible;
  if (visible) {
    _host->contents()->WasShown();
  } else {
    _host->contents()->WasHidden();
  }
}

- (void)loadURL:(NSURL*)URL {
  pane::Trace("call", "PaneWebContents.loadURL",
              base::DictValue()
                  .Set("webContents", base::SysNSStringToUTF8(TraceID(self)))
                  .Set("url", base::SysNSStringToUTF8(URL.absoluteString)));
  if (!_host) {
    return;
  }
  content::NavigationController::LoadURLParams params(net::GURLWithNSURL(URL));
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  _host->contents()->GetController().LoadURLWithParams(params);
}

- (void)close {
  pane::Trace("call", "PaneWebContents.close",
              base::DictValue().Set("webContents",
                                    base::SysNSStringToUTF8(TraceID(self))));
  [self.nativeView removeFromSuperview];
  _host.reset();
}

- (void)hostDidChangeTitle {
  NSString* title = self.title;
  pane::Trace("callback", "PaneWebContentsDelegate.didChangeTitle",
              base::DictValue()
                  .Set("webContents", base::SysNSStringToUTF8(TraceID(self)))
                  .Set("title", base::SysNSStringToUTF8(title)));
  if ([_delegate respondsToSelector:@selector(webContents:didChangeTitle:)]) {
    [_delegate webContents:self didChangeTitle:title];
  }
}

- (void)hostDidChangeURL {
  NSURL* URL = self.URL;
  pane::Trace("callback", "PaneWebContentsDelegate.didChangeURL",
              base::DictValue()
                  .Set("webContents", base::SysNSStringToUTF8(TraceID(self)))
                  .Set("url", base::SysNSStringToUTF8(URL.absoluteString)));
  if (URL && [_delegate respondsToSelector:@selector(webContents:
                                                       didChangeURL:)]) {
    [_delegate webContents:self didChangeURL:URL];
  }
}

- (void)hostDidChangeLoading:(BOOL)isLoading progress:(double)progress {
  pane::Trace("callback", "PaneWebContentsDelegate.didChangeLoading",
              base::DictValue()
                  .Set("webContents", base::SysNSStringToUTF8(TraceID(self)))
                  .Set("isLoading", static_cast<bool>(isLoading))
                  .Set("progress", progress));
  if ([_delegate respondsToSelector:@selector(webContents:
                                         didChangeLoading:progress:)]) {
    [_delegate webContents:self didChangeLoading:isLoading progress:progress];
  }
}

- (void)hostDidUpdateFavicon:(NSImage*)favicon {
  pane::Trace("callback", "PaneWebContentsDelegate.didUpdateFavicon",
              base::DictValue().Set("webContents",
                                    base::SysNSStringToUTF8(TraceID(self))));
  if ([_delegate respondsToSelector:@selector(webContents:
                                         didUpdateFavicon:)]) {
    [_delegate webContents:self didUpdateFavicon:favicon];
  }
}

@end
