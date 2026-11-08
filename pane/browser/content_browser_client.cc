// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/content_browser_client.h"

#include <utility>

#include "base/numerics/safe_conversions.h"
#include "chrome/browser/chrome_browser_main.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/core/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/generated_code_cache_settings.h"
#include "content/public/browser/web_contents.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "third_party/blink/public/common/web_preferences/web_preferences.h"
#include "pane/browser/browser_main_extra_parts.h"
#include "pane/browser/caches_in_memory.h"
#include "pane/browser/fingerprints.h"
#include "pane/encryption/handover.h"
#include "pane/common/host.h"

namespace pane {

ContentBrowserClient::ContentBrowserClient() = default;

ContentBrowserClient::~ContentBrowserClient() = default;

std::unique_ptr<content::BrowserMainParts>
ContentBrowserClient::CreateBrowserMainParts(bool is_integration_test) {
  std::unique_ptr<content::BrowserMainParts> parts =
      ChromeContentBrowserClient::CreateBrowserMainParts(is_integration_test);

  if (IsHosted()) {
    static_cast<ChromeBrowserMainParts*>(parts.get())
        ->AddParts(std::make_unique<BrowserMainExtraParts>());
  }

  return parts;
}

void ContentBrowserClient::AppendExtraCommandLineSwitches(
    base::CommandLine* command_line,
    int child_process_id) {
  ChromeContentBrowserClient::AppendExtraCommandLineSwitches(command_line,
                                                             child_process_id);
  Fingerprints::Get().AppendSwitches(command_line, child_process_id);
}

void ContentBrowserClient::ConfigureNetworkContextParams(
    content::BrowserContext* context,
    bool in_memory,
    const base::FilePath& relative_partition_path,
    network::mojom::NetworkContextParams* network_context_params,
    cert_verifier::mojom::CertVerifierCreationParams*
        cert_verifier_creation_params) {
  ChromeContentBrowserClient::ConfigureNetworkContextParams(
      context, in_memory, relative_partition_path, network_context_params,
      cert_verifier_creation_params);

  // The profile's own User-Agent, for a profile with a fingerprint.
  network_context_params->user_agent = GetUserAgentForBrowserContext(context);

  // Each time: a network service that restarts gets its keys back this way.
  network_context_params->pane_engine_keys =
      encryption::KeysForOtherProcess();

  // What the network service would keep on disk outside its stores' formats
  // stays in memory: the hosts it reached and its reports (ADR 0121).
  if (network_context_params->file_paths) {
    auto& paths = *network_context_params->file_paths;
    paths.http_server_properties_file_name.reset();
    paths.sct_auditing_pending_reports_file_name.reset();

    // A profile that saves no history keeps its cache in memory, as off the
    // record, cookies and all else on disk: each entry names its whole URL.
    if (KeepsCachesInMemory(context)) {
      paths.http_cache_directory.reset();
    }
  }
}

void ContentBrowserClient::OverrideWebPreferences(
    content::WebContents* web_contents,
    content::SiteInstance& main_frame_site,
    blink::web_pref::WebPreferences* web_prefs) {
  ChromeContentBrowserClient::OverrideWebPreferences(
      web_contents, main_frame_site, web_prefs);

  if (const Fingerprint* fingerprint =
          Fingerprints::Get().For(web_contents->GetBrowserContext())) {
    web_prefs->pointer_events_max_touch_points = fingerprint->max_touch_points;
  }
}

content::GeneratedCodeCacheSettings
ContentBrowserClient::GetGeneratedCodeCacheSettings(
    content::BrowserContext* context) {
  const content::GeneratedCodeCacheSettings settings =
      ChromeContentBrowserClient::GetGeneratedCodeCacheSettings(context);

  // A profile that saves no history keeps no compiled code on disk: each entry
  // names the script's URL.
  return content::GeneratedCodeCacheSettings(
      !KeepsCachesInMemory(context),
      base::checked_cast<int>(settings.size_in_bytes()), settings.path());
}

// A profile that saves no history keeps no record of a running download either:
// the cache holds its URL, page, and path, in a leveldb log a deletion does not
// rewrite. A crash loses the row that would have offered to resume it.
bool ContentBrowserClient::PersistsNewDownloads(
    content::BrowserContext* browser_context) {
  return !Profile::FromBrowserContext(browser_context)
              ->GetPrefs()
              ->GetBoolean(::prefs::kSavingBrowserHistoryDisabled);
}

std::string ContentBrowserClient::GetUserAgentForBrowserContext(
    content::BrowserContext* browser_context) {
  const Fingerprint* fingerprint = Fingerprints::Get().For(browser_context);
  return fingerprint ? fingerprint->user_agent : GetUserAgent();
}

blink::UserAgentMetadata
ContentBrowserClient::GetUserAgentMetadataForBrowserContext(
    content::BrowserContext* browser_context) {
  const Fingerprint* fingerprint = Fingerprints::Get().For(browser_context);
  return fingerprint ? fingerprint->user_agent_metadata
                     : GetUserAgentMetadata();
}

}  // namespace pane
