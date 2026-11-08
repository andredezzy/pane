// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_DOWNLOADS_H_
#define PANE_BROWSER_DOWNLOADS_H_

#include "components/download/content/public/all_download_item_notifier.h"

@class PaneBrowserContext;
class Profile;

namespace pane {

// Reports one profile's downloads to its PaneBrowserContext: every
// download's start, progress, and end.
class DownloadsBridge : public download::AllDownloadItemNotifier::Observer {
 public:
  DownloadsBridge(PaneBrowserContext* context, Profile* profile);
  DownloadsBridge(const DownloadsBridge&) = delete;
  DownloadsBridge& operator=(const DownloadsBridge&) = delete;
  ~DownloadsBridge() override;

  // download::AllDownloadItemNotifier::Observer:
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadUpdated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;

 private:
  void Report(download::DownloadItem* item);

  __weak PaneBrowserContext* context_;
  download::AllDownloadItemNotifier notifier_;
};

}  // namespace pane

#endif  // PANE_BROWSER_DOWNLOADS_H_
