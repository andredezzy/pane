// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/browser_context.h"

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_constants.h"
#include "chrome/browser/predictors/loading_predictor.h"
#include "chrome/browser/predictors/loading_predictor_factory.h"
#include "chrome/browser/predictors/predictor_database.h"
#include "chrome/browser/predictors/predictor_database_factory.h"
#include "chrome/browser/predictors/resource_prefetch_predictor.h"
#include "chrome/browser/predictors/resource_prefetch_predictor_tables.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/download/public/common/download_item.h"
#include "components/download/public/common/simple_download_manager.h"
#include "components/history/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "components/proxy_config/proxy_config_dictionary.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/download_manager.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/site_data.h"

namespace pane {

namespace {

// How long a clear waits for the loading predictor's database: past it, the
// clear is done all the same, and the next open of a profile that saves no
// history removes that database (ADR 0119).
constexpr base::TimeDelta kPredictorWait = base::Seconds(5);

// The rounds of that database's sequence a clear waits through: the one that
// runs the deletion History's news posted, and the next, which runs the one a
// predictor still loading posts once it has loaded.
constexpr int kPredictorRounds = 2;

// A clear's wait for the predictor's database, answered once: by its last
// round, or by the cap.
class PredictorWait : public base::RefCounted<PredictorWait> {
 public:
  PredictorWait(scoped_refptr<base::SequencedTaskRunner> database,
                base::OnceClosure done)
      : database_(std::move(database)),
        done_(std::move(done)),
        started_(base::TimeTicks::Now()) {}

  // Its next round: a task on the database's sequence, after every one
  // posted there before it, then back here.
  void Round(int left) {
    if (left == 0) {
      Answer(/*ran=*/true);
      return;
    }
    database_->PostTaskAndReply(
        FROM_HERE, base::DoNothing(),
        base::BindOnce(&PredictorWait::Round, base::WrapRefCounted(this),
                       left - 1));
  }

  void Answer(bool ran) {
    if (!done_) {
      return;
    }
    PANE_TRACE(
        "callback", "PaneBrowserContext.predictorCleared",
        base::DictValue()
            .Set("ran", ran)
            .Set("ms", (base::TimeTicks::Now() - started_).InMillisecondsF()));
    std::move(done_).Run();
  }

 private:
  friend class base::RefCounted<PredictorWait>;
  ~PredictorWait() = default;

  const scoped_refptr<base::SequencedTaskRunner> database_;
  base::OnceClosure done_;
  const base::TimeTicks started_;
};

// What the loading predictor learned of the profile's visits, the hosts in
// Network Action Predictor (ADR 0119), goes with a clear: as History tells it
// that everything went (ResourcePrefetchPredictor::OnHistoryDeletions), the
// predictor deletes it in a task on its database's sequence, a BEST_EFFORT
// one, and a profile's close cancels every task still waiting there
// (PredictorDatabase::Shutdown). A clear that turns history off is followed
// by the profile's close at once, to open again keeping its caches in memory
// (ProfileContexts::ReopenCachingInMemory): on a busy computer the deletion
// waited past the close, never reached the disk, and the profile that kept no
// history went on naming where it had been (profile-history, 3 to 4 runs in
// 6 in the test VM). So the clear is done once that sequence has run the
// deletion. A predictor still loading as the clear came watched no History
// yet: it is told to delete all it loads, and the deletion it posts once it
// has loaded is the second round's.
void WaitForPredictor(Profile* profile, base::OnceClosure done) {
  predictors::LoadingPredictor* loading =
      predictors::LoadingPredictorFactory::GetForProfile(profile);
  predictors::PredictorDatabase* database =
      loading ? predictors::PredictorDatabaseFactory::GetForProfile(profile)
              : nullptr;
  if (!database) {
    std::move(done).Run();
    return;
  }
  loading->resource_prefetch_predictor()->DeleteAllUrls();
  auto wait = base::MakeRefCounted<PredictorWait>(
      base::WrapRefCounted(
          database->resource_prefetch_tables()->GetTaskRunner()),
      std::move(done));
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, base::BindOnce(&PredictorWait::Answer, wait, false),
      kPredictorWait);
  wait->Round(kPredictorRounds);
}

}  // namespace

BrowserContext::BrowserContext(std::string id, Profile* profile)
    : id_(std::move(id)),
      profile_(profile),
      // A Pane context counts as an open window for its profile, so Chrome
      // does not unload the profile while the app shows it.
      keep_alive_(std::make_unique<ScopedProfileKeepAlive>(
          profile,
          ProfileKeepAliveOrigin::kBrowserWindow)) {}

BrowserContext::~BrowserContext() = default;

std::unique_ptr<ScopedProfileKeepAlive> BrowserContext::TakeKeepAlive() {
  return std::move(keep_alive_);
}

bool BrowserContext::SavesHistory() const {
  return !profile_->GetPrefs()->GetBoolean(
      prefs::kSavingBrowserHistoryDisabled);
}

void BrowserContext::SetSavesHistory(bool saves) {
  PANE_TRACE("call", "PaneBrowserContext.setSavesHistory",
             base::DictValue().Set("savesHistory", saves));
  profile_->GetPrefs()->SetBoolean(prefs::kSavingBrowserHistoryDisabled,
                                   !saves);
}

void BrowserContext::ClearHistory(base::OnceClosure done) {
  PANE_TRACE("call", "PaneBrowserContext.clearHistory");
  // Site usage: the engagement scores, whose keys name each site, which the
  // history alone leaves. The cache: each entry names its whole URL. Then the
  // loading predictor's deletion, on the disk before the clear is done.
  RemoveSiteData(
      profile_,
      chrome_browsing_data_remover::DATA_TYPE_HISTORY |
          chrome_browsing_data_remover::DATA_TYPE_SITE_USAGE_DATA |
          content::BrowsingDataRemover::DATA_TYPE_DOWNLOADS |
          content::BrowsingDataRemover::DATA_TYPE_CACHE,
      base::BindOnce(
          [](base::WeakPtr<BrowserContext> context, base::OnceClosure done) {
            // A profile closing meanwhile (the remover answers as it goes)
            // cancels what its predictor still had to write: the next open of
            // a profile that saves no history removes it.
            if (!context || context->profile()->ShutdownStarted()) {
              std::move(done).Run();
              return;
            }
            WaitForPredictor(context->profile(), std::move(done));
          },
          weak_factory_.GetWeakPtr(), std::move(done)));
}

bool BrowserContext::HasRunningDownloads() const {
  download::SimpleDownloadManager::DownloadVector items;
  profile_->GetDownloadManager()->GetAllDownloads(&items);
  for (const download::DownloadItem* item : items) {
    if (item->GetState() == download::DownloadItem::IN_PROGRESS) {
      return true;
    }
  }
  return false;
}

void BrowserContext::SetProxyRules(const std::optional<std::string>& rules) {
  PrefService* prefs = profile_->GetPrefs();
  if (rules && !rules->empty()) {
    prefs->SetDict(
        proxy_config::prefs::kProxy,
        ProxyConfigDictionary::CreateFixedServers(*rules, std::string()));
  } else {
    prefs->ClearPref(proxy_config::prefs::kProxy);
  }
}

void BrowserContext::SetWebRTCIPHandlingPolicy(std::string_view policy) {
  profile_->GetPrefs()->SetString(prefs::kWebRTCIPHandlingPolicy, policy);
}

}  // namespace pane
