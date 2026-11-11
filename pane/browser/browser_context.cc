// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/browser_context.h"

#include <utility>
#include <vector>

#include "base/functional/callback.h"
#include "base/values.h"
#include "chrome/browser/browsing_data/chrome_browsing_data_remover_constants.h"
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
#include "components/proxy_config/proxy_prefs.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/download_manager.h"
#include "pane/browser/engine_trace.h"
#include "pane/browser/site_data.h"

namespace pane {

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
  // history alone leaves. The cache: each entry names its whole URL.
  RemoveSiteData(profile_,
                 chrome_browsing_data_remover::DATA_TYPE_HISTORY |
                     chrome_browsing_data_remover::DATA_TYPE_SITE_USAGE_DATA |
                     content::BrowsingDataRemover::DATA_TYPE_DOWNLOADS |
                     content::BrowsingDataRemover::DATA_TYPE_CACHE,
                 {}, std::move(done));
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

std::optional<std::string> BrowserContext::ProxyRules() const {
  ProxyConfigDictionary config(
      profile_->GetPrefs()->GetDict(proxy_config::prefs::kProxy).Clone());
  std::string server;
  ProxyPrefs::ProxyMode mode;
  if (!config.GetMode(&mode) || mode != ProxyPrefs::MODE_FIXED_SERVERS ||
      !config.GetProxyServer(&server)) {
    return std::nullopt;
  }
  return server;
}

void BrowserContext::SetProxyRules(const std::optional<std::string>& rules) {
  PANE_TRACE("call", "PaneBrowserContext.setProxyRules",
             base::DictValue().Set("rules", rules.value_or(std::string())));
  PrefService* prefs = profile_->GetPrefs();
  if (rules && !rules->empty()) {
    prefs->SetDict(
        proxy_config::prefs::kProxy,
        ProxyConfigDictionary::CreateFixedServers(*rules, std::string()));
  } else {
    prefs->ClearPref(proxy_config::prefs::kProxy);
  }
}

std::string BrowserContext::WebRTCIPHandlingPolicy() const {
  return profile_->GetPrefs()->GetString(prefs::kWebRTCIPHandlingPolicy);
}

void BrowserContext::SetWebRTCIPHandlingPolicy(std::string_view policy) {
  PANE_TRACE("call", "PaneBrowserContext.setWebRTCIPHandlingPolicy",
             base::DictValue().Set("policy", policy));
  profile_->GetPrefs()->SetString(prefs::kWebRTCIPHandlingPolicy, policy);
}

}  // namespace pane
