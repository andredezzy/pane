// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/site_data.h"

#include <memory>
#include <utility>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"

namespace pane {

namespace {

// Runs its callback once the removal it observes is done, then deletes
// itself. The remover only reports to observers added to it.
class RemovalObserver : public content::BrowsingDataRemover::Observer {
 public:
  RemovalObserver(content::BrowsingDataRemover* remover, base::OnceClosure done)
      : remover_(remover), done_(std::move(done)) {
    remover_->AddObserver(this);
  }

  void OnBrowsingDataRemoverDone(uint64_t failed_data_types) override {
    remover_->RemoveObserver(this);
    std::move(done_).Run();
    delete this;
  }

 private:
  ~RemovalObserver() override = default;

  raw_ptr<content::BrowsingDataRemover> remover_;
  base::OnceClosure done_;
};

}  // namespace

void RemoveSiteData(content::BrowserContext* browser_context,
                    uint64_t remove_mask,
                    const std::vector<url::Origin>& origins,
                    base::OnceClosure done) {
  std::unique_ptr<content::BrowsingDataFilterBuilder> filter =
      content::BrowsingDataFilterBuilder::Create(
          origins.empty()
              ? content::BrowsingDataFilterBuilder::Mode::kPreserve
              : content::BrowsingDataFilterBuilder::Mode::kDelete);
  for (const url::Origin& origin : origins) {
    filter->AddOrigin(origin);
  }

  content::BrowsingDataRemover* remover =
      browser_context->GetBrowsingDataRemover();
  remover->RemoveWithFilterAndReply(
      base::Time(), base::Time::Max(), remove_mask,
      content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB |
          content::BrowsingDataRemover::ORIGIN_TYPE_PROTECTED_WEB,
      std::move(filter), new RemovalObserver(remover, std::move(done)));
}

}  // namespace pane
