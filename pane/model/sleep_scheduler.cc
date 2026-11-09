// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/model/sleep_scheduler.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/time/clock.h"
#include "pane/model/app_settings.h"

namespace pane::model {

namespace {

constexpr int64_t kBytesPerMB = 1024 * 1024;

// The log keeps no profile's id or path, which the Swift app's logger
// redacted: only that a trim failed.
void LogWarning(const std::string& warning) {
  LOG(ERROR) << "Trimming a profile's cache failed.";
}

// Whether `minutes` have passed since `id`'s stamp, when it has one.
bool Elapsed(const std::map<std::string, base::Time>& since,
             const std::string& id,
             int minutes,
             base::Time now) {
  const auto stamp = since.find(id);
  return stamp != since.end() && now - stamp->second >= base::Minutes(minutes);
}

}  // namespace

SleepScheduler::SleepScheduler(const std::vector<std::string>& profile_ids,
                               std::optional<std::string> active_profile_id,
                               std::optional<std::string> active_tab_id,
                               const base::Clock* clock,
                               WarnCallback warn)
    : clock_(clock),
      warn_(warn ? std::move(warn) : base::BindRepeating(&LogWarning)),
      active_profile_id_(std::move(active_profile_id)),
      active_tab_id_(std::move(active_tab_id)) {
  const base::Time start = clock_->Now();
  for (const std::string& id : profile_ids) {
    if (id != active_profile_id_) {
      profile_hidden_since_[id] = start;
    }
  }
}

SleepScheduler::~SleepScheduler() = default;

void SleepScheduler::ActiveDidChange(std::optional<std::string> profile_id,
                                     std::optional<std::string> tab_id) {
  const base::Time now = clock_->Now();

  if (profile_id != active_profile_id_) {
    if (active_profile_id_) {
      profile_hidden_since_[*active_profile_id_] = now;
    }
    if (profile_id) {
      profile_hidden_since_.erase(*profile_id);
    }
  }

  if (tab_id != active_tab_id_) {
    if (active_tab_id_) {
      tab_background_since_[*active_tab_id_] = now;
    }
    if (tab_id) {
      tab_background_since_.erase(*tab_id);
    }
  }

  active_profile_id_ = std::move(profile_id);
  active_tab_id_ = std::move(tab_id);
}

void SleepScheduler::Tick(const std::map<std::string, Profile>& profiles,
                          const AppSettings& settings) {
  const base::Time now = clock_->Now();

  for (const auto& [profile_id, profile] : profiles) {
    const std::vector<std::string> loaded = profile.tabs->LoadedTabIds();

    // A tab loaded in the background (a link opened in a new tab, a page's or
    // an extension's) was never active, so no selection started its clock:
    // the first tick that sees it does.
    for (const std::string& tab_id : loaded) {
      if (tab_id != active_tab_id_) {
        tab_background_since_.try_emplace(tab_id, now);
      }
    }

    if (loaded.empty() || profile.keep_loaded) {
      continue;
    }

    const bool hidden = profile_id != active_profile_id_;
    if (settings.profile_unload_after_minutes && hidden &&
        Elapsed(profile_hidden_since_, profile_id,
                *settings.profile_unload_after_minutes, now) &&
        std::ranges::none_of(loaded, [&](const std::string& tab_id) {
          return profile.tabs->IsProtected(tab_id);
        })) {
      profile.tabs->UnloadAll();
      Trim(profile_id, profile.cache, settings.cache_budget_mb);

      // The hidden clock restarts on the next hide: a tab appearing in a
      // still-hidden profile (opened by an extension, say) must not unload at
      // once against this stale stamp.
      profile_hidden_since_.erase(profile_id);

      continue;
    }

    if (!settings.tab_sleep_after_minutes) {
      continue;
    }

    for (const std::string& tab_id : loaded) {
      if (tab_id != active_tab_id_ &&
          Elapsed(tab_background_since_, tab_id,
                  *settings.tab_sleep_after_minutes, now) &&
          !profile.tabs->IsProtected(tab_id)) {
        profile.tabs->Sleep(tab_id);
        tab_background_since_.erase(tab_id);
      }
    }

    // A hidden profile whose last tabs just slept has unloaded too, and is
    // trimmed as one. A hidden profile's tabs are all in the background, so
    // with tab sleep sooner than unload (the defaults) they always drain
    // first; the Electron scheduler then never trimmed at all.
    if (settings.profile_unload_after_minutes && hidden &&
        profile.tabs->LoadedTabIds().empty()) {
      Trim(profile_id, profile.cache, settings.cache_budget_mb);
      profile_hidden_since_.erase(profile_id);
    }
  }

  PruneTabStamps(profiles);
}

// Started, never waited on: a slow disk must not stall the tick. The in-flight
// set stops a later tick stacking a second trim on the same profile. A failed
// trim is logged and dropped; the next unload retries.
void SleepScheduler::Trim(const std::string& profile_id,
                          base::WeakPtr<TrimmableCache> cache,
                          std::optional<int> budget_mb) {
  if (!budget_mb || !cache || trimming_.contains(profile_id)) {
    return;
  }

  trimming_.insert(profile_id);

  const int64_t budget = int64_t{*budget_mb} * kBytesPerMB;
  cache->ComputeSize(base::BindOnce(&SleepScheduler::OnCacheSize,
                                    weak_factory_.GetWeakPtr(), profile_id,
                                    cache, budget));
}

void SleepScheduler::OnCacheSize(const std::string& profile_id,
                                 base::WeakPtr<TrimmableCache> cache,
                                 int64_t budget,
                                 base::expected<int64_t, std::string> size) {
  if (!size.has_value()) {
    OnTrimmed(profile_id, base::unexpected(std::move(size).error()));
    return;
  }
  if (*size <= budget || !cache) {
    OnTrimmed(profile_id, base::ok());
    return;
  }
  cache->Trim(base::BindOnce(&SleepScheduler::OnTrimmed,
                             weak_factory_.GetWeakPtr(), profile_id));
}

void SleepScheduler::OnTrimmed(const std::string& profile_id,
                               base::expected<void, std::string> result) {
  if (!result.has_value()) {
    warn_.Run(base::StrCat({"Trimming the cache of profile ", profile_id,
                            " failed: ", result.error()}));
  }
  trimming_.erase(profile_id);
}

void SleepScheduler::PruneTabStamps(
    const std::map<std::string, Profile>& profiles) {
  std::set<std::string> loaded;
  for (const auto& [profile_id, profile] : profiles) {
    for (std::string& tab_id : profile.tabs->LoadedTabIds()) {
      loaded.insert(std::move(tab_id));
    }
  }
  std::erase_if(tab_background_since_, [&](const auto& stamp) {
    return !loaded.contains(stamp.first);
  });
}

}  // namespace pane::model
