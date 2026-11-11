// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/proxy_test.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/location.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "content/public/browser/network_service_instance.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/auth.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "pane/browser/socks_relay.h"
#include "pane/model/profile_form.h"
#include "pane/model/proxy_route.h"
#include "pane/model/test_hooks.h"
#include "services/network/public/cpp/originating_process_id.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace pane {

namespace {

// The Electron app's echo; a scenario's own in its run.
constexpr char kTestURL[] = "https://httpbin.org/ip";
// What the echo answers is a few bytes.
constexpr size_t kMaxResponse = 64 * 1024;
constexpr base::TimeDelta kTimeout = base::Seconds(10);

constexpr net::NetworkTrafficAnnotationTag kProxyTestTraffic =
    net::DefineNetworkTrafficAnnotation("pane_proxy_test", R"(
      semantics {
        sender: "Pane's proxy test"
        description:
          "Fetches an IP echo through a profile's proxy, to show whether the "
          "proxy works and the address sites see (ADR 0037)."
        trigger:
          "A proxied profile's proxy saved or changed, the data's opening "
          "at launch, and the profile sheet's Test connection."
        data: "None: a plain GET, through the proxy the user gave."
        destination: OTHER
        destination_other: "httpbin.org, through the profile's proxy."
      }
      policy {
        cookies_allowed: NO
        setting: "The profile's proxy, in its sheet."
        policy_exception_justification: "Pane has no enterprise policy."
      })");

GURL TestURL() {
  return GURL(model::TestHook("PANE_PROXY_TEST_URL").value_or(kTestURL));
}

// What a failure is called beside the dot, in the sheet's words: Paper's
// "Connection refused" for a proxy that won't take the connection, the
// Swift's "Connection timed out", and otherwise the error's own name, as a
// phrase ("ERR_TUNNEL_CONNECTION_FAILED": "Tunnel connection failed").
std::string ErrorText(int error) {
  switch (error) {
    case net::ERR_TIMED_OUT:
    case net::ERR_CONNECTION_TIMED_OUT:
      return "Connection timed out";
    case net::ERR_PROXY_CONNECTION_FAILED:
    case net::ERR_SOCKS_CONNECTION_FAILED:
    case net::ERR_CONNECTION_REFUSED:
      return "Connection refused";
    case net::ERR_PROXY_AUTH_REQUESTED:
    case net::ERR_PROXY_AUTH_UNSUPPORTED:
      return "The proxy refused the credentials";
    default:
      break;
  }
  std::string name = net::ErrorToShortString(error);
  if (name.starts_with("net::")) {
    name = name.substr(5);
  }
  if (name.starts_with("ERR_")) {
    name = name.substr(4);
  }
  base::ReplaceChars(name, "_", " ", &name);
  name = base::ToLowerASCII(name);
  if (!name.empty()) {
    name[0] = base::ToUpperASCII(name[0]);
  }
  return name;
}

}  // namespace

ProxyTest::ProxyTest(const model::ProxyConfig& proxy, Done done)
    : proxy_(proxy), done_(std::move(done)) {
  // A port outside 1 to 65535 never reaches Chrome's rules, which would
  // drop it and go direct (ADR 0037).
  if (!model::IsProxyPort(proxy_.port)) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(&ProxyTest::Finish, weak_factory_.GetWeakPtr(),
                       Outcome{.error = "bad port"}));
    return;
  }
  if (model::NeedsRelay(proxy_)) {
    relay_ = std::make_unique<SocksRelay>(
        proxy_,
        base::BindOnce(&ProxyTest::RelayStarted, weak_factory_.GetWeakPtr()));
    return;
  }
  // Posted, so `done` never runs while this is made.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&ProxyTest::Fetch, weak_factory_.GetWeakPtr(),
                                model::ProxyRules(proxy_, std::nullopt)));
}

ProxyTest::~ProxyTest() = default;

void ProxyTest::RelayStarted(std::optional<int> port) {
  if (!port) {
    Finish({.error = "The relay did not start"});
    return;
  }
  Fetch(model::ProxyRules(proxy_, *port));
}

void ProxyTest::Fetch(const std::string& rules) {
  SystemNetworkContextManager* manager =
      g_browser_process->system_network_context_manager();
  if (!manager) {
    Finish({.error = "No network"});
    return;
  }
  // A network context of its own, which keeps nothing, with the proxy as
  // its one rule: what it reaches, it reaches through the proxy.
  network::mojom::NetworkContextParamsPtr params =
      manager->CreateDefaultNetworkContextParams();
  params->http_cache_enabled = false;
  net::ProxyConfig config;
  config.proxy_rules().ParseFromString(rules);
  params->initial_proxy_config =
      net::ProxyConfigWithAnnotation(config, kProxyTestTraffic);
  content::CreateNetworkContextInNetworkService(
      context_.BindNewPipeAndPassReceiver(), std::move(params));

  network::mojom::URLLoaderFactoryParamsPtr factory_params =
      network::mojom::URLLoaderFactoryParams::New();
  factory_params->process_id = network::OriginatingProcessId::browser();
  factory_params->is_orb_enabled = false;
  factory_params->is_trusted = true;
  observers_.Add(this, factory_params->url_loader_network_observer
                           .InitWithNewPipeAndPassReceiver());
  context_->CreateURLLoaderFactory(factory_.BindNewPipeAndPassReceiver(),
                                   std::move(factory_params));

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = TestURL();
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_DISABLE_CACHE;
  loader_ =
      network::SimpleURLLoader::Create(std::move(request), kProxyTestTraffic);
  loader_->SetTimeoutDuration(kTimeout);
  // The echo's status, whatever it is.
  loader_->SetAllowHttpErrorResults(true);
  loader_->DownloadToString(
      factory_.get(),
      base::BindOnce(&ProxyTest::Fetched, weak_factory_.GetWeakPtr()),
      kMaxResponse);
}

void ProxyTest::Fetched(std::optional<std::string> body) {
  const network::mojom::URLResponseHead* head = loader_->ResponseInfo();
  const int status = head && head->headers ? head->headers->response_code() : 0;
  if (!body) {
    Finish({.error = ErrorText(loader_->NetError())});
    return;
  }
  if (status != 200) {
    Finish({.error = base::StrCat({"HTTP ", base::NumberToString(status)})});
    return;
  }
  const std::optional<base::Value> echo =
      base::JSONReader::Read(*body, base::JSON_PARSE_RFC);
  if (!echo || !echo->is_dict()) {
    Finish({.error = "Invalid response"});
    return;
  }
  const std::string* origin = echo->GetDict().FindString("origin");
  Finish({.ip = origin ? *origin : "Unknown"});
}

void ProxyTest::Finish(Outcome outcome) {
  loader_.reset();
  factory_.reset();
  context_.reset();
  observers_.Clear();
  relay_.reset();
  if (done_) {
    std::move(done_).Run(outcome);
  }
}

void ProxyTest::OnSSLCertificateError(const GURL& url,
                                      int net_error,
                                      const net::SSLInfo& ssl_info,
                                      bool fatal,
                                      OnSSLCertificateErrorCallback response) {
  std::move(response).Run(net_error);
}

void ProxyTest::OnCertificateRequested(
    const std::optional<base::UnguessableToken>& window_id,
    const scoped_refptr<net::SSLCertRequestInfo>& cert_info,
    mojo::PendingRemote<network::mojom::ClientCertificateResponder>
        cert_responder) {
  mojo::Remote<network::mojom::ClientCertificateResponder>(
      std::move(cert_responder))
      ->CancelRequest();
}

void ProxyTest::OnAuthRequired(
    const std::optional<base::UnguessableToken>& window_id,
    int32_t request_id,
    const GURL& url,
    bool first_auth_attempt,
    bool do_not_prompt_for_login,
    const net::AuthChallengeInfo& auth_info,
    const scoped_refptr<net::HttpResponseHeaders>& head_headers,
    mojo::PendingRemote<network::mojom::AuthChallengeResponder>
        auth_challenge_responder) {
  mojo::Remote<network::mojom::AuthChallengeResponder> responder(
      std::move(auth_challenge_responder));
  // The proxy's own challenge, once: asked again, the credentials were
  // wrong, and the test fails on its 407.
  const std::string username = proxy_.username.value_or("");
  if (!auth_info.is_proxy || answered_ || username.empty()) {
    responder->OnAuthCredentials(std::nullopt);
    return;
  }
  answered_ = true;
  responder->OnAuthCredentials(
      net::AuthCredentials(base::UTF8ToUTF16(username),
                           base::UTF8ToUTF16(proxy_.password.value_or(""))));
}

void ProxyTest::OnLocalNetworkAccessPermissionRequired(
    network::mojom::TransportType transport_type,
    network::mojom::IPAddressSpace ip_address_space,
    OnLocalNetworkAccessPermissionRequiredCallback callback) {
  std::move(callback).Run(network::mojom::LocalNetworkAccessResult::kGranted);
}

void ProxyTest::OnPlatformLocalNetworkPermissionRequired(
    OnPlatformLocalNetworkPermissionRequiredCallback callback) {
  std::move(callback).Run(true);
}

void ProxyTest::OnClearSiteData(
    const GURL& url,
    const std::optional<net::SchemefulSite>& top_level_site,
    const std::optional<base::UnguessableToken>& nonce,
    const std::string& header_value,
    int load_flags,
    const std::optional<net::CookiePartitionKey>& cookie_partition_key,
    bool partitioned_state_allowed_only,
    OnClearSiteDataCallback callback) {
  std::move(callback).Run();
}

void ProxyTest::OnLoadingStateUpdate(network::mojom::LoadInfoPtr info,
                                     OnLoadingStateUpdateCallback callback) {
  std::move(callback).Run();
}

void ProxyTest::OnDataUseUpdate(int32_t network_traffic_annotation_id_hash,
                                base::ByteSize recv_bytes,
                                base::ByteSize sent_bytes) {}

void ProxyTest::Clone(
    mojo::PendingReceiver<network::mojom::URLLoaderNetworkServiceObserver>
        listener) {
  observers_.Add(this, std::move(listener));
}

void ProxyTest::OnWebSocketConnectedToLocalNetwork(
    const GURL& request_url,
    network::mojom::IPAddressSpace ip_address_space) {}

void ProxyTest::OnUrlLoaderConnectedToLocalNetwork(
    const GURL& request_url,
    network::mojom::IPAddressSpace response_address_space,
    network::mojom::IPAddressSpace client_address_space,
    network::mojom::IPAddressSpace target_address_space) {}

}  // namespace pane
