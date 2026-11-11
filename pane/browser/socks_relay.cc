// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/browser/socks_relay.h"

#include <stdint.h>

#include <algorithm>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/containers/unique_ptr_adapters.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/address_list.h"
#include "net/base/host_port_pair.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/network_anonymization_key.h"
#include "net/base/network_handle.h"
#include "net/dns/public/host_resolver_results.h"
#include "net/dns/public/resolve_error_info.h"
#include "net/log/net_log_source.h"
#include "net/socket/stream_socket.h"
#include "net/socket/tcp_client_socket.h"
#include "net/socket/tcp_server_socket.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "pane/model/profile_form.h"
#include "pane/model/proxy_route.h"
#include "services/network/public/mojom/host_resolver.mojom.h"
#include "services/network/public/mojom/network_context.mojom.h"

namespace pane {

namespace {

using Bytes = std::vector<uint8_t>;

constexpr int kBacklog = 64;
// What the proxy has to take a connection in: found, reached, and signed in
// to (the Swift's relay gave its connect as long).
constexpr base::TimeDelta kUpstreamTimeout = base::Seconds(15);
// A direction's chunk, as the Swift's readSome took one.
constexpr int kPipeChunk = 64 * 1024;
// An accept that failed, as when the process is out of file descriptors,
// is tried again a moment later rather than in a loop.
constexpr base::TimeDelta kAcceptRetry = base::Milliseconds(100);

constexpr net::NetworkTrafficAnnotationTag kRelayTraffic =
    net::DefineNetworkTrafficAnnotation("pane_socks_relay", R"(
      semantics {
        sender: "Pane's SOCKS relay"
        description:
          "Carries a profile's connections through its SOCKS proxy, signing "
          "in with the credentials Chrome can't send one (ADR 0037)."
        trigger:
          "A page or request of a profile whose proxy is SOCKS with a "
          "username."
        data:
          "What the profile sends, and the proxy's username and password, to "
          "the proxy the user gave."
        destination: OTHER
        destination_other: "The profile's proxy."
      }
      policy {
        cookies_allowed: NO
        setting: "The profile's proxy, in its sheet."
        policy_exception_justification: "Pane has no enterprise policy."
      })");

// SOCKS5's answers to Chrome: granted, or a general failure, each with a
// bound address of 0.0.0.0:0, which Chrome needs not.
constexpr uint8_t kGranted[] = {0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
constexpr uint8_t kRefused[] = {0x05, 0x01, 0x00, 0x01, 0, 0, 0, 0, 0, 0};

// SOCKS5's address types.
constexpr uint8_t kIPv4 = 0x01;
constexpr uint8_t kName = 0x03;
constexpr uint8_t kIPv6 = 0x04;

Bytes ToBytes(std::string_view text, size_t most) {
  const std::string_view kept = text.substr(0, std::min(text.size(), most));
  return Bytes(kept.begin(), kept.end());
}

// A CONNECT's destination, as SOCKS5 names it.
struct Destination {
  uint8_t type = kIPv4;
  // 4 or 16 bytes, or the name.
  Bytes address;
  uint16_t port = 0;

  Bytes PortBytes() const {
    return {static_cast<uint8_t>(port >> 8), static_cast<uint8_t>(port & 0xFF)};
  }

  // Its type, then the address: a name after its length, at most 255 bytes.
  Bytes Socks5Address() const {
    Bytes bytes = {type};
    if (type == kName) {
      const size_t length = std::min<size_t>(address.size(), 255);
      bytes.push_back(static_cast<uint8_t>(length));
      bytes.insert(bytes.end(), address.begin(), address.begin() + length);
    } else {
      bytes.insert(bytes.end(), address.begin(), address.end());
    }
    return bytes;
  }
};

}  // namespace

// On the IO thread: the listening socket, and each connection Chrome makes
// to it.
class SocksRelay::Core {
 public:
  class Connection;

  Core(model::ProxyConfig upstream,
       mojo::PendingRemote<network::mojom::HostResolver> resolver)
      : upstream_(std::move(upstream)) {
    if (resolver) {
      resolver_.Bind(std::move(resolver));
    }
  }
  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;
  ~Core();

  // Listens on 127.0.0.1, at a port the system picks: that port, or
  // nullopt.
  std::optional<int> Listen() {
    server_ =
        std::make_unique<net::TCPServerSocket>(nullptr, net::NetLogSource());
    net::IPEndPoint local;
    if (server_->Listen(net::IPEndPoint(net::IPAddress::IPv4Localhost(), 0),
                        kBacklog, /*ipv6_only=*/std::nullopt) != net::OK ||
        server_->GetLocalAddress(&local) != net::OK) {
      server_.reset();
      return std::nullopt;
    }
    Accept();
    return local.port();
  }

  const model::ProxyConfig& upstream() const { return upstream_; }

  // The network service's resolver for the proxy's host, while it answers.
  network::mojom::HostResolver* resolver() {
    return resolver_.is_bound() && resolver_.is_connected() ? resolver_.get()
                                                            : nullptr;
  }

  // A connection ended: it goes once the call that ended it has returned.
  void Ended(Connection* connection);

 private:
  void Accept() {
    while (server_) {
      const int result = server_->Accept(
          &accepted_,
          base::BindOnce(&Core::Accepted, weak_factory_.GetWeakPtr()));
      if (result == net::ERR_IO_PENDING || !Take(result)) {
        return;
      }
    }
  }

  void Accepted(int result) {
    if (Take(result)) {
      Accept();
    }
  }

  // Whether to accept the next one at once.
  bool Take(int result);

  const model::ProxyConfig upstream_;
  mojo::Remote<network::mojom::HostResolver> resolver_;
  std::unique_ptr<net::TCPServerSocket> server_;
  std::unique_ptr<net::StreamSocket> accepted_;
  std::set<std::unique_ptr<Connection>, base::UniquePtrComparator> connections_;
  base::WeakPtrFactory<Core> weak_factory_{this};
};

// One connection of Chrome's: its SOCKS5 greeting with no authentication,
// its CONNECT, then the proxy reached and signed in to, then the two piped
// both ways until either closes.
class SocksRelay::Core::Connection : public network::mojom::ResolveHostClient {
 public:
  Connection(Core* core, std::unique_ptr<net::StreamSocket> client)
      : core_(core), client_(std::move(client)) {}
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  ~Connection() override = default;

  void Start() {
    Read(client_.get(), 2,
         base::BindOnce(&Connection::GreetingRead, base::Unretained(this)));
  }

  // network::mojom::ResolveHostClient:
  void OnComplete(int32_t result,
                  const net::ResolveErrorInfo& resolve_error_info,
                  const net::AddressList& addresses,
                  const std::vector<net::HostResolverEndpointResult>&
                      alternative_endpoints) override {
    resolving_.reset();
    if (result != net::OK || addresses.empty()) {
      Refuse("the proxy's host was not found");
      return;
    }
    Connect(net::AddressList::CopyWithPort(
        addresses, static_cast<uint16_t>(core_->upstream().port)));
  }
  void OnTextResults(const std::vector<std::string>& text_results) override {}
  void OnHostnameResults(const std::vector<net::HostPortPair>& hosts) override {
  }

 private:
  // What one side sends copied to the other, until either closes.
  class Pump {
   public:
    Pump(Connection* connection, net::StreamSocket* from, net::StreamSocket* to)
        : connection_(connection),
          from_(from),
          to_(to),
          chunk_(base::MakeRefCounted<net::IOBufferWithSize>(kPipeChunk)) {}

    void Run() {
      while (true) {
        const int result = from_->Read(
            chunk_.get(), kPipeChunk,
            base::BindOnce(&Pump::Received, base::Unretained(this)));
        if (result == net::ERR_IO_PENDING) {
          return;
        }
        if (!Forward(result)) {
          return;
        }
      }
    }

   private:
    void Received(int result) {
      if (Forward(result)) {
        Run();
      }
    }

    // Writes what came, all of it; whether to read on at once.
    bool Forward(int received) {
      if (received <= 0) {
        connection_->End();
        return false;
      }
      sending_ = base::MakeRefCounted<net::DrainableIOBuffer>(chunk_, received);
      return Send();
    }

    bool Send() {
      while (sending_->BytesRemaining() > 0) {
        const int result = to_->Write(
            sending_.get(), sending_->BytesRemaining(),
            base::BindOnce(&Pump::Sent, base::Unretained(this)), kRelayTraffic);
        if (result == net::ERR_IO_PENDING) {
          return false;
        }
        if (result <= 0) {
          connection_->End();
          return false;
        }
        sending_->DidConsume(result);
      }
      return true;
    }

    void Sent(int result) {
      if (result <= 0) {
        connection_->End();
        return;
      }
      sending_->DidConsume(result);
      if (Send()) {
        Run();
      }
    }

    const raw_ptr<Connection> connection_;
    const raw_ptr<net::StreamSocket> from_;
    const raw_ptr<net::StreamSocket> to_;
    const scoped_refptr<net::IOBufferWithSize> chunk_;
    scoped_refptr<net::DrainableIOBuffer> sending_;
  };

  // Reads exactly `count` bytes of `socket`, then runs `then` with them; a
  // close or an error ends the connection.
  void Read(net::StreamSocket* socket,
            size_t count,
            base::OnceCallback<void(Bytes)> then) {
    if (count == 0) {
      std::move(then).Run(Bytes());
      return;
    }
    read_socket_ = socket;
    read_then_ = std::move(then);
    read_buffer_ = base::MakeRefCounted<net::IOBufferWithSize>(count);
    reading_ = base::MakeRefCounted<net::DrainableIOBuffer>(
        read_buffer_, read_buffer_->size());
    ReadOn();
  }

  void ReadOn() {
    while (true) {
      const int result = read_socket_->Read(
          reading_.get(), reading_->BytesRemaining(),
          base::BindOnce(&Connection::ReadSome, base::Unretained(this)));
      if (result == net::ERR_IO_PENDING || !TakeRead(result)) {
        return;
      }
    }
  }

  void ReadSome(int result) {
    if (TakeRead(result)) {
      ReadOn();
    }
  }

  // Whether to read on: false once the read is whole, its `then` run, or
  // the connection has ended.
  bool TakeRead(int result) {
    if (result <= 0) {
      End();
      return false;
    }
    reading_->DidConsume(result);
    if (reading_->BytesRemaining() > 0) {
      return true;
    }
    const base::span<const uint8_t> read = read_buffer_->span();
    Bytes bytes(read.begin(), read.end());
    reading_.reset();
    read_buffer_.reset();
    std::move(read_then_).Run(std::move(bytes));
    return false;
  }

  // Writes all of `bytes` to `socket`, then runs `then`; an error ends the
  // connection.
  void Write(net::StreamSocket* socket, Bytes bytes, base::OnceClosure then) {
    write_socket_ = socket;
    write_then_ = std::move(then);
    auto buffer = base::MakeRefCounted<net::IOBufferWithSize>(bytes.size());
    buffer->span().copy_from(bytes);
    writing_ =
        base::MakeRefCounted<net::DrainableIOBuffer>(buffer, buffer->size());
    WriteOn();
  }

  void WriteOn() {
    while (writing_->BytesRemaining() > 0) {
      const int result = write_socket_->Write(
          writing_.get(), writing_->BytesRemaining(),
          base::BindOnce(&Connection::WroteSome, base::Unretained(this)),
          kRelayTraffic);
      if (result == net::ERR_IO_PENDING) {
        return;
      }
      if (result <= 0) {
        End();
        return;
      }
      writing_->DidConsume(result);
    }
    writing_.reset();
    std::move(write_then_).Run();
  }

  void WroteSome(int result) {
    if (result <= 0) {
      End();
      return;
    }
    writing_->DidConsume(result);
    WriteOn();
  }

  // MARK: Chrome's side

  void GreetingRead(Bytes greeting) {
    if (greeting[0] != 0x05) {
      End();
      return;
    }
    // The methods it offers: whatever they are, it gets no authentication.
    Read(client_.get(), greeting[1],
         base::BindOnce(&Connection::MethodsRead, base::Unretained(this)));
  }

  void MethodsRead(Bytes methods) {
    Write(client_.get(), {0x05, 0x00},
          base::BindOnce(&Connection::Greeted, base::Unretained(this)));
  }

  void Greeted() {
    Read(client_.get(), 4,
         base::BindOnce(&Connection::RequestRead, base::Unretained(this)));
  }

  // Only CONNECT is relayed.
  void RequestRead(Bytes request) {
    if (request[0] != 0x05 || request[1] != 0x01) {
      End();
      return;
    }
    destination_.type = request[3];
    switch (destination_.type) {
      case kIPv4:
        Read(client_.get(), 4 + 2,
             base::BindOnce(&Connection::AddressRead, base::Unretained(this)));
        return;
      case kIPv6:
        Read(client_.get(), 16 + 2,
             base::BindOnce(&Connection::AddressRead, base::Unretained(this)));
        return;
      case kName:
        Read(client_.get(), 1,
             base::BindOnce(&Connection::NameLengthRead,
                            base::Unretained(this)));
        return;
      default:
        End();
    }
  }

  void NameLengthRead(Bytes length) {
    Read(client_.get(), length[0] + 2,
         base::BindOnce(&Connection::AddressRead, base::Unretained(this)));
  }

  // The address, then its port.
  void AddressRead(Bytes address) {
    const size_t port_at = address.size() - 2;
    destination_.port =
        static_cast<uint16_t>(address[port_at] << 8 | address[port_at + 1]);
    address.resize(port_at);
    destination_.address = std::move(address);
    ReachUpstream();
  }

  // MARK: The proxy's side

  void ReachUpstream() {
    const model::ProxyConfig& upstream = core_->upstream();
    // A port out of range is refused rather than trapped on (ADR 0037).
    if (!model::IsProxyPort(upstream.port)) {
      Refuse("bad proxy port");
      return;
    }
    if (upstream.type != model::ProxyType::kSocks5 &&
        upstream.type != model::ProxyType::kSocks4) {
      Refuse("not a SOCKS proxy");
      return;
    }
    timeout_.Start(FROM_HERE, kUpstreamTimeout,
                   base::BindOnce(&Connection::Refuse, base::Unretained(this),
                                  "timed out"));
    const std::string host = model::ConnectionHost(upstream);
    net::IPAddress literal;
    if (literal.AssignFromIPLiteral(host)) {
      Connect(net::AddressList(
          net::IPEndPoint(literal, static_cast<uint16_t>(upstream.port))));
      return;
    }
    network::mojom::HostResolver* resolver = core_->resolver();
    if (!resolver) {
      Refuse("the proxy's host was not found");
      return;
    }
    resolver->ResolveHost(
        network::mojom::HostResolverHost::NewHostPortPair(
            net::HostPortPair(host, static_cast<uint16_t>(upstream.port))),
        net::NetworkAnonymizationKey::CreateTransient(), nullptr,
        resolving_.BindNewPipeAndPassRemote());
    resolving_.set_disconnect_handler(
        base::BindOnce(&Connection::Refuse, base::Unretained(this),
                       "the proxy's host was not found"));
  }

  void Connect(const net::AddressList& addresses) {
    upstream_socket_ = std::make_unique<net::TCPClientSocket>(
        addresses, nullptr, nullptr, nullptr, net::NetLogSource(),
        net::handles::kInvalidNetworkHandle);
    const int result = upstream_socket_->Connect(
        base::BindOnce(&Connection::Connected, base::Unretained(this)));
    if (result != net::ERR_IO_PENDING) {
      Connected(result);
    }
  }

  void Connected(int result) {
    if (result != net::OK) {
      Refuse("the proxy did not answer");
      return;
    }
    if (core_->upstream().type == model::ProxyType::kSocks4) {
      Socks4Connect();
    } else {
      Write(upstream_socket_.get(), {0x05, 0x02, 0x00, 0x02},
            base::BindOnce(&Connection::Socks5Greeted, base::Unretained(this)));
    }
  }

  // RFC 1928, with a username and password (RFC 1929).
  void Socks5Greeted() {
    Read(upstream_socket_.get(), 2,
         base::BindOnce(&Connection::Socks5Method, base::Unretained(this)));
  }

  void Socks5Method(Bytes method) {
    switch (method[1]) {
      case 0x00:
        Socks5Connect();
        return;
      case 0x02: {
        const model::ProxyConfig& upstream = core_->upstream();
        const Bytes user = ToBytes(upstream.username.value_or(""), 255);
        const Bytes password = ToBytes(upstream.password.value_or(""), 255);
        Bytes request = {0x01, static_cast<uint8_t>(user.size())};
        request.insert(request.end(), user.begin(), user.end());
        request.push_back(static_cast<uint8_t>(password.size()));
        request.insert(request.end(), password.begin(), password.end());
        Write(upstream_socket_.get(), std::move(request),
              base::BindOnce(&Connection::Socks5SignedIn,
                             base::Unretained(this)));
        return;
      }
      default:
        Refuse("the proxy offers no usable authentication");
    }
  }

  void Socks5SignedIn() {
    Read(upstream_socket_.get(), 2,
         base::BindOnce(&Connection::Socks5SignInRead, base::Unretained(this)));
  }

  void Socks5SignInRead(Bytes status) {
    if (status[1] != 0x00) {
      Refuse("the proxy refused the credentials");
      return;
    }
    Socks5Connect();
  }

  void Socks5Connect() {
    Bytes request = {0x05, 0x01, 0x00};
    const Bytes address = destination_.Socks5Address();
    request.insert(request.end(), address.begin(), address.end());
    const Bytes port = destination_.PortBytes();
    request.insert(request.end(), port.begin(), port.end());
    Write(upstream_socket_.get(), std::move(request),
          base::BindOnce(&Connection::Socks5Requested, base::Unretained(this)));
  }

  void Socks5Requested() {
    Read(upstream_socket_.get(), 4,
         base::BindOnce(&Connection::Socks5Replied, base::Unretained(this)));
  }

  // The bound address after it, which nothing needs.
  void Socks5Replied(Bytes reply) {
    if (reply[1] != 0x00) {
      Refuse("the proxy refused the connection");
      return;
    }
    switch (reply[3]) {
      case kIPv4:
        Read(upstream_socket_.get(), 4 + 2,
             base::BindOnce(&Connection::Socks5Bound, base::Unretained(this)));
        return;
      case kIPv6:
        Read(upstream_socket_.get(), 16 + 2,
             base::BindOnce(&Connection::Socks5Bound, base::Unretained(this)));
        return;
      case kName:
        Read(upstream_socket_.get(), 1,
             base::BindOnce(&Connection::Socks5BoundName,
                            base::Unretained(this)));
        return;
      default:
        Refuse("unknown address type");
    }
  }

  void Socks5BoundName(Bytes length) {
    Read(upstream_socket_.get(), length[0] + 2,
         base::BindOnce(&Connection::Socks5Bound, base::Unretained(this)));
  }

  void Socks5Bound(Bytes address) { Established(); }

  // SOCKS4a: the user id, and the host's name when it has no IPv4 address.
  void Socks4Connect() {
    const Bytes user = ToBytes(core_->upstream().username.value_or(""), 255);
    Bytes request = {0x04, 0x01};
    const Bytes port = destination_.PortBytes();
    request.insert(request.end(), port.begin(), port.end());
    switch (destination_.type) {
      case kIPv4:
        request.insert(request.end(), destination_.address.begin(),
                       destination_.address.end());
        request.insert(request.end(), user.begin(), user.end());
        request.push_back(0x00);
        break;
      case kName:
        request.insert(request.end(), {0, 0, 0, 1});
        request.insert(request.end(), user.begin(), user.end());
        request.push_back(0x00);
        request.insert(request.end(), destination_.address.begin(),
                       destination_.address.end());
        request.push_back(0x00);
        break;
      default:
        Refuse("SOCKS4 has no IPv6");
        return;
    }
    Write(upstream_socket_.get(), std::move(request),
          base::BindOnce(&Connection::Socks4Requested, base::Unretained(this)));
  }

  void Socks4Requested() {
    Read(upstream_socket_.get(), 8,
         base::BindOnce(&Connection::Socks4Replied, base::Unretained(this)));
  }

  void Socks4Replied(Bytes reply) {
    if (reply[1] != 0x5A) {
      Refuse("the proxy refused the connection");
      return;
    }
    Established();
  }

  // The proxy has the connection: Chrome hears so, then each side's bytes go
  // to the other.
  void Established() {
    timeout_.Stop();
    answered_ = true;
    Write(client_.get(), Bytes(std::begin(kGranted), std::end(kGranted)),
          base::BindOnce(&Connection::Pipe, base::Unretained(this)));
  }

  void Pipe() {
    up_ = std::make_unique<Pump>(this, client_.get(), upstream_socket_.get());
    down_ = std::make_unique<Pump>(this, upstream_socket_.get(), client_.get());
    up_->Run();
    // The first pump may have ended the connection already.
    if (!ended_) {
      down_->Run();
    }
  }

  // Chrome hears a general failure, then the connection ends.
  void Refuse(const char* why) {
    if (ended_ || answered_) {
      End();
      return;
    }
    VLOG(1) << "Pane's SOCKS relay refused a connection: " << why;
    answered_ = true;
    timeout_.Stop();
    resolving_.reset();
    // The proxy's side goes first, what it had under way with it, so the
    // refusal is the one write left.
    if (upstream_socket_) {
      upstream_socket_->Disconnect();
    }
    Write(client_.get(), Bytes(std::begin(kRefused), std::end(kRefused)),
          base::BindOnce(&Connection::End, base::Unretained(this)));
  }

  // Ends at once, never again: every socket's pending work is cancelled,
  // and the connection goes once the call that ended it has returned.
  void End() {
    if (ended_) {
      return;
    }
    ended_ = true;
    timeout_.Stop();
    resolving_.reset();
    if (client_) {
      client_->Disconnect();
    }
    if (upstream_socket_) {
      upstream_socket_->Disconnect();
    }
    core_->Ended(this);
  }

  const raw_ptr<Core> core_;
  std::unique_ptr<net::StreamSocket> client_;
  std::unique_ptr<net::StreamSocket> upstream_socket_;
  Destination destination_;
  // The read and the write under way, while the two shake hands.
  raw_ptr<net::StreamSocket> read_socket_ = nullptr;
  scoped_refptr<net::IOBufferWithSize> read_buffer_;
  scoped_refptr<net::DrainableIOBuffer> reading_;
  base::OnceCallback<void(Bytes)> read_then_;
  raw_ptr<net::StreamSocket> write_socket_ = nullptr;
  scoped_refptr<net::DrainableIOBuffer> writing_;
  base::OnceClosure write_then_;
  base::OneShotTimer timeout_;
  mojo::Receiver<network::mojom::ResolveHostClient> resolving_{this};
  std::unique_ptr<Pump> up_;
  std::unique_ptr<Pump> down_;
  // Chrome has its answer to the CONNECT.
  bool answered_ = false;
  bool ended_ = false;
};

SocksRelay::Core::~Core() = default;

bool SocksRelay::Core::Take(int result) {
  if (result != net::OK) {
    VLOG(1) << "Pane's SOCKS relay could not accept: "
            << net::ErrorToShortString(result);
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, base::BindOnce(&Core::Accept, weak_factory_.GetWeakPtr()),
        kAcceptRetry);
    return false;
  }
  auto connection = std::make_unique<Connection>(this, std::move(accepted_));
  Connection* started = connection.get();
  connections_.insert(std::move(connection));
  started->Start();
  return true;
}

void SocksRelay::Core::Ended(Connection* connection) {
  const auto found = connections_.find(connection);
  if (found == connections_.end()) {
    return;
  }
  std::unique_ptr<Connection> ended =
      std::move(connections_.extract(found).value());
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE,
                                                             std::move(ended));
}

SocksRelay::SocksRelay(const model::ProxyConfig& upstream, Started started)
    : upstream_(upstream) {
  // The proxy's host, when it is a name, is found at each connection, as
  // Chrome finds a proxy's: by the network service.
  mojo::PendingRemote<network::mojom::HostResolver> resolver;
  net::IPAddress literal;
  if (!literal.AssignFromIPLiteral(model::ConnectionHost(upstream))) {
    if (SystemNetworkContextManager* manager =
            g_browser_process->system_network_context_manager()) {
      manager->GetContext()->CreateHostResolver(
          std::nullopt, resolver.InitWithNewPipeAndPassReceiver());
    }
  }
  core_ = base::SequenceBound<Core>(content::GetIOThreadTaskRunner({}),
                                    upstream, std::move(resolver));
  core_.AsyncCall(&Core::Listen).Then(std::move(started));
}

SocksRelay::~SocksRelay() = default;

}  // namespace pane
