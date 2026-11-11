// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_PROXY_TEST_H_
#define PANE_BROWSER_PROXY_TEST_H_

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "pane/model/profile.h"
#include "services/network/public/mojom/network_context.mojom.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/mojom/url_loader_network_service_observer.mojom.h"

namespace network {
class SimpleURLLoader;
}

namespace pane {

class SocksRelay;

// A proxy's connection test (the Electron app's proxy-test.ts, then the
// Swift app's ProxyTest, ADR 0037): an IP echo fetched through the proxy as
// Chrome will reach it, a SOCKS proxy with a username through a relay of its
// own, in a network context of its own that keeps nothing, within 10 s. Its
// answer is the sidebar's dot and the profile sheet's line beside Test
// connection. Scenarios point it at their own echo (PANE_PROXY_TEST_URL).
class ProxyTest : public network::mojom::URLLoaderNetworkServiceObserver {
 public:
  struct Outcome {
    // The address the echo saw, once it connected.
    std::optional<std::string> ip;
    // Why it failed, otherwise: "Connection refused", "HTTP 502".
    std::string error;
  };
  using Done = base::OnceCallback<void(const Outcome& outcome)>;

  // Starts at once; `done` runs on the UI thread, never during this call.
  // Gone first, it ends the test, and `done` never runs.
  ProxyTest(const model::ProxyConfig& proxy, Done done);
  ProxyTest(const ProxyTest&) = delete;
  ProxyTest& operator=(const ProxyTest&) = delete;
  ~ProxyTest() override;

  const model::ProxyConfig& proxy() const { return proxy_; }

  // network::mojom::URLLoaderNetworkServiceObserver: a proxy asking for
  // credentials gets the profile's, once.
  void OnSSLCertificateError(const GURL& url,
                             int net_error,
                             const net::SSLInfo& ssl_info,
                             bool fatal,
                             OnSSLCertificateErrorCallback response) override;
  void OnCertificateRequested(
      const std::optional<base::UnguessableToken>& window_id,
      const scoped_refptr<net::SSLCertRequestInfo>& cert_info,
      mojo::PendingRemote<network::mojom::ClientCertificateResponder>
          cert_responder) override;
  void OnAuthRequired(
      const std::optional<base::UnguessableToken>& window_id,
      int32_t request_id,
      const GURL& url,
      bool first_auth_attempt,
      bool do_not_prompt_for_login,
      const net::AuthChallengeInfo& auth_info,
      const scoped_refptr<net::HttpResponseHeaders>& head_headers,
      mojo::PendingRemote<network::mojom::AuthChallengeResponder>
          auth_challenge_responder) override;
  void OnLocalNetworkAccessPermissionRequired(
      network::mojom::TransportType transport_type,
      network::mojom::IPAddressSpace ip_address_space,
      OnLocalNetworkAccessPermissionRequiredCallback callback) override;
  void OnPlatformLocalNetworkPermissionRequired(
      OnPlatformLocalNetworkPermissionRequiredCallback callback) override;
  void OnClearSiteData(
      const GURL& url,
      const std::optional<net::SchemefulSite>& top_level_site,
      const std::optional<base::UnguessableToken>& nonce,
      const std::string& header_value,
      int load_flags,
      const std::optional<net::CookiePartitionKey>& cookie_partition_key,
      bool partitioned_state_allowed_only,
      OnClearSiteDataCallback callback) override;
  void OnLoadingStateUpdate(network::mojom::LoadInfoPtr info,
                            OnLoadingStateUpdateCallback callback) override;
  void OnDataUseUpdate(int32_t network_traffic_annotation_id_hash,
                       base::ByteSize recv_bytes,
                       base::ByteSize sent_bytes) override;
  void Clone(
      mojo::PendingReceiver<network::mojom::URLLoaderNetworkServiceObserver>
          listener) override;
  void OnWebSocketConnectedToLocalNetwork(
      const GURL& request_url,
      network::mojom::IPAddressSpace ip_address_space) override;
  void OnUrlLoaderConnectedToLocalNetwork(
      const GURL& request_url,
      network::mojom::IPAddressSpace response_address_space,
      network::mojom::IPAddressSpace client_address_space,
      network::mojom::IPAddressSpace target_address_space) override;

 private:
  void RelayStarted(std::optional<int> port);
  // Fetches the echo through `rules`, Chrome's fixed proxy rules.
  void Fetch(const std::string& rules);
  void Fetched(std::optional<std::string> body);
  void Finish(Outcome outcome);

  const model::ProxyConfig proxy_;
  Done done_;
  std::unique_ptr<SocksRelay> relay_;
  mojo::Remote<network::mojom::NetworkContext> context_;
  mojo::Remote<network::mojom::URLLoaderFactory> factory_;
  mojo::ReceiverSet<network::mojom::URLLoaderNetworkServiceObserver>
      observers_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  // The proxy asked for credentials, and had them.
  bool answered_ = false;
  base::WeakPtrFactory<ProxyTest> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_BROWSER_PROXY_TEST_H_
