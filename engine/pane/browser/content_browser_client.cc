// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/content_browser_client.h"

#include <utility>

#include "chrome/browser/chrome_browser_main.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "pane/browser/browser_main_extra_parts.h"
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

  // Each time: a network service that restarts gets its keys back this way.
  network_context_params->pane_engine_keys =
      encryption::KeysForOtherProcess();

  // What the network service would keep on disk outside its stores' formats
  // stays in memory: the hosts it reached and its reports (ADR 0121).
  if (network_context_params->file_paths) {
    auto& paths = *network_context_params->file_paths;
    paths.http_server_properties_file_name.reset();
    paths.sct_auditing_pending_reports_file_name.reset();
  }
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
