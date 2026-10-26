// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_CONTENT_BROWSER_CLIENT_H_
#define PANE_BROWSER_CONTENT_BROWSER_CLIENT_H_

#include <memory>
#include <string>

#include "chrome/browser/chrome_content_browser_client.h"

namespace pane {

// Chrome's browser client, plus the Pane parts that run when the Pane app
// hosts the engine.
class ContentBrowserClient : public ChromeContentBrowserClient {
 public:
  ContentBrowserClient();
  ContentBrowserClient(const ContentBrowserClient&) = delete;
  ContentBrowserClient& operator=(const ContentBrowserClient&) = delete;
  ~ContentBrowserClient() override;

  // ChromeContentBrowserClient:
  std::unique_ptr<content::BrowserMainParts> CreateBrowserMainParts(
      bool is_integration_test) override;
  void AppendExtraCommandLineSwitches(base::CommandLine* command_line,
                                      int child_process_id) override;
  std::string GetUserAgentForBrowserContext(
      content::BrowserContext* browser_context) override;
  blink::UserAgentMetadata GetUserAgentMetadataForBrowserContext(
      content::BrowserContext* browser_context) override;
};

}  // namespace pane

#endif  // PANE_BROWSER_CONTENT_BROWSER_CLIENT_H_
