// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/profile_cache.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/storage_partition.h"
#include "mojo/public/cpp/bindings/callback_helpers.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/site_data.h"
#include "services/network/public/mojom/network_context.mojom.h"

namespace pane {

void ComputeCacheSize(Profile* profile,
                      base::OnceCallback<void(int64_t bytes)> done) {
  if (!profile) {
    std::move(done).Run(0);
    return;
  }
  // Mojo drops a reply whose pipe closes (the network service restarting,
  // the profile going): `done` runs then too, with nothing (ADR 0017).
  profile->GetDefaultStoragePartition()
      ->GetNetworkContext()
      ->ComputeHttpCacheSize(
          base::Time(), base::Time::Max(),
          mojo::WrapCallbackWithDefaultInvokeIfNotRun(
              base::BindOnce(
                  [](base::OnceCallback<void(int64_t)> done,
                     bool is_upper_bound, int64_t size_or_error) {
                    std::move(done).Run(size_or_error < 0 ? 0 : size_or_error);
                  },
                  std::move(done)),
              false, int64_t{0}));
}

void TrimCache(Profile* profile, base::OnceClosure done) {
  PANE_TRACE("call", "PaneBrowserContext.trimCache");
  if (!profile) {
    std::move(done).Run();
    return;
  }
  RemoveSiteData(profile,
                 content::BrowsingDataRemover::DATA_TYPE_CACHE |
                     content::BrowsingDataRemover::DATA_TYPE_CACHE_STORAGE |
                     content::BrowsingDataRemover::DATA_TYPE_SERVICE_WORKERS,
                 std::move(done));
}

}  // namespace pane
