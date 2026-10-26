// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/content_browser_client.h"

#include <utility>

#include "chrome/browser/chrome_browser_main.h"
#include "pane/browser/browser_main_extra_parts.h"
#include "pane/browser/fingerprints.h"
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
