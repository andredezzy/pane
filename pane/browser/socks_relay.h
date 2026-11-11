// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#ifndef PANE_BROWSER_SOCKS_RELAY_H_
#define PANE_BROWSER_SOCKS_RELAY_H_

#include <memory>
#include <optional>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequence_bound.h"
#include "pane/model/profile.h"

namespace pane {

// A local SOCKS5 server, on loopback alone, which Chrome reaches without
// credentials, that takes each connection on through a profile's SOCKS5
// proxy with its username and password (RFC 1929), or its SOCKS4a one with
// its user id: Chromium can't sign in to a SOCKS proxy (ADR 0037). The
// Electron app's proxy-relay.ts, then the Swift app's on Network.framework,
// on //net's sockets on the browser's IO thread, the proxy's host found by
// the network service's resolver at each connection. A port outside 1 to
// 65535 refuses each connection ("bad proxy port") rather than trapping, so
// a profile's pages fail and every other profile goes on.
class SocksRelay {
 public:
  // The port it listens on, on the UI thread; nullopt when it could not.
  using Started = base::OnceCallback<void(std::optional<int> port)>;

  // Starts listening, on a port the system picks.
  SocksRelay(const model::ProxyConfig& upstream, Started started);
  SocksRelay(const SocksRelay&) = delete;
  SocksRelay& operator=(const SocksRelay&) = delete;
  // Stops listening; the connections under way end.
  ~SocksRelay();

  const model::ProxyConfig& upstream() const { return upstream_; }

 private:
  class Core;

  const model::ProxyConfig upstream_;
  base::SequenceBound<Core> core_;
  base::WeakPtrFactory<SocksRelay> weak_factory_{this};
};

}  // namespace pane

#endif  // PANE_BROWSER_SOCKS_RELAY_H_
