// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_SYSTEM_PROXY_H_
#define PANE_BROWSER_SYSTEM_PROXY_H_

#include <memory>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ref.h"
#include "base/values.h"
#include "net/proxy_resolution/proxy_config_service.h"

class PrefService;

namespace net {
class ProxyConfig;
class ProxyConfigWithAnnotation;
}  // namespace net

namespace network::mojom {
class NetworkContextParams;
}  // namespace network::mojom

namespace pane {

// Pane follows the system's proxy settings without their auto-detection, on
// every system (ADR 0040, the user's call of 2026-11-15): Windows' explicit
// proxy server and its PAC address (the setup script), live as Windows changes
// them, but never "Automatically detect settings", which Windows ships on and
// which asks the network for `wpad` at each start (DHCP, then DNS), so anyone
// on a public network could answer and take the traffic. Chromium reads the
// settings (net::ProxyConfigService's system reading); Pane drops the
// detection from what that reading says, and nothing else. A profile's own
// proxy, a policy's, the command line's, and an extension's are not the
// system's, and stay as they are, with their precedence.

// The system's settings in `config` without their auto-detection: their PAC
// address, or their servers and bypass list, or direct when detection was all
// they held. Any settings not read from the system come back as they are.
net::ProxyConfigWithAnnotation WithoutAutoDetection(
    const net::ProxyConfigWithAnnotation& config);

// The "proxy" pref's value (Chrome's ProxyConfigDictionary) for the system's
// settings `system`, without their auto-detection: a PAC address, servers and
// their bypass list, or direct. A pref holds one of those, so where the system
// gives a PAC address and servers both, the PAC address alone.
base::DictValue ProxyPrefWithoutAutoDetection(const net::ProxyConfig& system);

// Puts WithoutAutoDetection between Chrome's proxy settings for a profile's
// network context and the context, for as long as the context takes them:
// what the context starts with, and each change Chrome sends it after.
void DropAutoDetection(network::mojom::NetworkContextParams& params);

// The browser's own network contexts, which Pane's update check and Chrome's
// Safe Browsing lists go out through, take their proxy from local state's
// "proxy" pref, and from the system's settings, detection and all, where that
// pref is not set; no seam reaches what they send their contexts. So this keeps
// that pref as the system's settings say, without their auto-detection, while
// the browser runs: what Chromium's own system reading reports, each time it
// changes. A policy's or the command line's proxy outranks the pref, as it
// outranks the system's settings. On the UI thread.
class SystemProxy : public net::ProxyConfigService::Observer {
 public:
  // Before the browser's network contexts exist: local state's pref holds
  // the last launch's reading, or direct until the first, so those contexts
  // never take the system's settings, detection and all.
  static void HoldLocalState(PrefService& local_state);

  // Keeps local state's pref as `system`, Chromium's reading of the system's
  // settings, reports them.
  SystemProxy(PrefService& local_state,
              std::unique_ptr<net::ProxyConfigService> system);
  SystemProxy(const SystemProxy&) = delete;
  SystemProxy& operator=(const SystemProxy&) = delete;
  ~SystemProxy() override;

  // The browser's, while it runs; null before and after.
  static SystemProxy* Get();

  // Runs `read` once the system's settings have been read, at once if they
  // have: until then local state holds the last launch's reading.
  void WhenRead(base::OnceClosure read);

 private:
  // net::ProxyConfigService::Observer:
  void OnProxyConfigChanged(
      const net::ProxyConfigWithAnnotation& config,
      net::ProxyConfigService::ConfigAvailability availability) override;

  void Read(const net::ProxyConfig& system);

  const raw_ref<PrefService> local_state_;
  const std::unique_ptr<net::ProxyConfigService> system_;
  bool read_ = false;
  std::vector<base::OnceClosure> waiting_;
};

}  // namespace pane

#endif  // PANE_BROWSER_SYSTEM_PROXY_H_
