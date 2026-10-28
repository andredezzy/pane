// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/downloads.h"

#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_manager.h"
#include "net/base/apple/url_conversions.h"
#include "pane/browser/engine_trace.h"

#import "pane/browser/browser_context_internal.h"

@interface PaneDownload ()
- (instancetype)initWithItem:(download::DownloadItem*)item;
@end

@implementation PaneDownload {
  NSString* _identifier;
  NSURL* _URL;
  NSURL* _fileURL;
  int64_t _receivedBytes;
  int64_t _totalBytes;
  PaneDownloadState _state;
}

- (instancetype)initWithItem:(download::DownloadItem*)item {
  if ((self = [super init])) {
    _identifier = base::SysUTF8ToNSString(item->GetGuid());
    _URL = net::NSURLWithGURL(item->GetURL());
    const base::FilePath& target = item->GetTargetFilePath();
    _fileURL = target.empty()
                   ? nil
                   : [NSURL fileURLWithPath:base::SysUTF8ToNSString(
                                                target.value())];
    _receivedBytes = item->GetReceivedBytes();
    _totalBytes = item->GetTotalBytes() > 0 ? item->GetTotalBytes() : -1;
    switch (item->GetState()) {
      case download::DownloadItem::COMPLETE:
        _state = PaneDownloadStateComplete;
        break;
      case download::DownloadItem::CANCELLED:
        _state = PaneDownloadStateCancelled;
        break;
      case download::DownloadItem::INTERRUPTED:
        _state = PaneDownloadStateInterrupted;
        break;
      default:
        _state = PaneDownloadStateInProgress;
        break;
    }
  }
  return self;
}

- (NSString*)identifier {
  return _identifier;
}

- (NSURL*)URL {
  return _URL;
}

- (NSURL*)fileURL {
  return _fileURL;
}

- (int64_t)receivedBytes {
  return _receivedBytes;
}

- (int64_t)totalBytes {
  return _totalBytes;
}

- (PaneDownloadState)state {
  return _state;
}

@end

namespace pane {

DownloadsBridge::DownloadsBridge(PaneBrowserContext* context, Profile* profile)
    : context_(context), notifier_(profile->GetDownloadManager(), this) {}

DownloadsBridge::~DownloadsBridge() = default;

void DownloadsBridge::OnDownloadCreated(content::DownloadManager* manager,
                                        download::DownloadItem* item) {
  Report(item);
}

void DownloadsBridge::OnDownloadUpdated(content::DownloadManager* manager,
                                        download::DownloadItem* item) {
  Report(item);
}

void DownloadsBridge::Report(download::DownloadItem* item) {
  PaneBrowserContext* context = context_;
  PaneDownload* download = [[PaneDownload alloc] initWithItem:item];
  PANE_TRACE(
      "callback", "PaneBrowserContextDelegate.didUpdateDownload",
      base::DictValue()
          .Set("identifier", item->GetGuid())
          .Set("state", static_cast<int>(download.state))
          .Set("receivedBytes", static_cast<double>(download.receivedBytes))
          .Set("file", item->GetTargetFilePath().value()));
  id<PaneBrowserContextDelegate> delegate = context.delegate;
  if ([delegate respondsToSelector:@selector(browserContext:
                                          didUpdateDownload:)]) {
    [delegate browserContext:context didUpdateDownload:download];
  }
}

}  // namespace pane
