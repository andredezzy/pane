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
  void ConfigureNetworkContextParams(
      content::BrowserContext* context,
      bool in_memory,
      const base::FilePath& relative_partition_path,
      network::mojom::NetworkContextParams* network_context_params,
      cert_verifier::mojom::CertVerifierCreationParams*
          cert_verifier_creation_params) override;
  void OverrideWebPreferences(
      content::WebContents* web_contents,
      content::SiteInstance& main_frame_site,
      blink::web_pref::WebPreferences* web_prefs) override;
  content::GeneratedCodeCacheSettings GetGeneratedCodeCacheSettings(
      content::BrowserContext* context) override;
  bool PersistsNewDownloads(content::BrowserContext* browser_context) override;
  std::string GetUserAgentForBrowserContext(
      content::BrowserContext* browser_context) override;
  blink::UserAgentMetadata GetUserAgentMetadataForBrowserContext(
      content::BrowserContext* browser_context) override;

 protected:
  // HTTP and proxy sign-in in Pane's window (pane/ui/dialogs/sign_in.h).
  std::unique_ptr<HttpAuthCoordinator> CreateHttpAuthCoordinator() override;
};

}  // namespace pane

#endif  // PANE_BROWSER_CONTENT_BROWSER_CLIENT_H_
