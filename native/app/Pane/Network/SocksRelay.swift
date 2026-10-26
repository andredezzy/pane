import Foundation
import Network
import PaneKit

// A local SOCKS5 server without authentication, which Chrome connects to,
// that reaches each destination through a profile's SOCKS4 or SOCKS5 proxy
// with its credentials (apps/veil profile/proxy-relay.ts): Chromium cannot
// authenticate to a SOCKS proxy. It listens on loopback only.
final class SocksRelay: @unchecked Sendable {
	private let upstream: ProxyConfig
	private let listener: NWListener
	private let queue = DispatchQueue(label: "pane.socks-relay")

	init(upstream: ProxyConfig) throws {
		self.upstream = upstream

		let parameters = NWParameters.tcp
		parameters.requiredLocalEndpoint = .hostPort(host: "127.0.0.1", port: .any)
		listener = try NWListener(using: parameters)
	}

	// Starts listening, and returns the local port.
	func start() async throws -> Int {
		listener.newConnectionHandler = { [weak self] client in
			guard let self else {
				client.cancel()
				return
			}

			Task { await self.serve(client) }
		}

		return try await withCheckedThrowingContinuation { continuation in
			let once = Once()

			listener.stateUpdateHandler = { [listener] state in
				switch state {
				case .ready:
					once.run { continuation.resume(returning: Int(listener.port?.rawValue ?? 0)) }
				case let .failed(error):
					once.run { continuation.resume(throwing: error) }
				case .cancelled:
					once.run { continuation.resume(throwing: RelayError("the relay stopped")) }
				default:
					break
				}
			}

			listener.start(queue: queue)
		}
	}

	func stop() {
		listener.cancel()
	}

	// One client: the SOCKS5 greeting (no authentication), its CONNECT, then
	// the upstream connection, piped both ways.
	private func serve(_ client: NWConnection) async {
		do {
			try await client.open(on: queue)

			let greeting = try await client.read(2)

			guard greeting[0] == 0x05 else {
				throw RelayError("not SOCKS5")
			}

			_ = try await client.read(Int(greeting[1]))
			try await client.write([0x05, 0x00])

			let request = try await client.read(4)

			guard request[0] == 0x05, request[1] == 0x01 else {
				throw RelayError("only CONNECT is relayed")
			}

			let destination = try await Destination.read(type: request[3], from: client)
			let remote: NWConnection

			do {
				remote = try await connectUpstream(to: destination)
			} catch {
				try? await client.write([0x05, 0x01, 0x00, 0x01, 0, 0, 0, 0, 0, 0])
				throw error
			}

			try await client.write([0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0])
			pipe(client, remote)
		} catch {
			client.cancel()
		}
	}

	private func connectUpstream(to destination: Destination) async throws -> NWConnection {
		guard let port = NWEndpoint.Port(rawValue: UInt16(upstream.port)) else {
			throw RelayError("bad proxy port")
		}

		let remote = NWConnection(host: NWEndpoint.Host(upstream.host), port: port, using: .tcp)

		do {
			try await remote.open(on: queue, timeout: 15)

			switch upstream.proxyType {
			case .socks5:
				try await socks5Handshake(remote, destination)
			case .socks4:
				try await socks4Handshake(remote, destination)
			case .http, .https:
				throw RelayError("not a SOCKS proxy")
			}

			return remote
		} catch {
			remote.cancel()
			throw error
		}
	}

	// RFC 1928 with username and password (RFC 1929).
	private func socks5Handshake(_ remote: NWConnection, _ destination: Destination) async throws {
		try await remote.write([0x05, 0x02, 0x00, 0x02])
		let method = try await remote.read(2)

		switch method[1] {
		case 0x00:
			break
		case 0x02:
			let user = Array((upstream.username ?? "").utf8.prefix(255))
			let password = Array((upstream.password ?? "").utf8.prefix(255))
			try await remote.write([0x01, UInt8(user.count)] + user + [UInt8(password.count)] + password)
			let status = try await remote.read(2)

			guard status[1] == 0x00 else {
				throw RelayError("the proxy refused the credentials")
			}
		default:
			throw RelayError("the proxy offers no usable authentication")
		}

		try await remote.write([0x05, 0x01, 0x00] + destination.socks5Address + destination.portBytes)
		let reply = try await remote.read(4)

		guard reply[1] == 0x00 else {
			throw RelayError("the proxy refused the connection (\(reply[1]))")
		}

		// The bound address, which nothing needs.
		_ = try await Destination.read(type: reply[3], from: remote)
	}

	// SOCKS4a: the user ID, and the host name when there is no IPv4 address.
	private func socks4Handshake(_ remote: NWConnection, _ destination: Destination) async throws {
		let user = Array((upstream.username ?? "").utf8)
		var request: [UInt8] = [0x04, 0x01] + destination.portBytes

		switch destination.host {
		case let .ipv4(bytes):
			request += bytes + user + [0x00]
		case let .name(name):
			request += [0, 0, 0, 1] + user + [0x00] + Array(name.utf8) + [0x00]
		case .ipv6:
			throw RelayError("SOCKS4 has no IPv6")
		}

		try await remote.write(request)
		let reply = try await remote.read(8)

		guard reply[1] == 0x5A else {
			throw RelayError("the proxy refused the connection (\(reply[1]))")
		}
	}

	// Copies each side to the other until either closes.
	private func pipe(_ a: NWConnection, _ b: NWConnection) {
		Task { await copy(from: a, to: b) }
		Task { await copy(from: b, to: a) }
	}

	private func copy(from source: NWConnection, to target: NWConnection) async {
		while true {
			guard let chunk = try? await source.readSome(), !chunk.isEmpty else {
				source.cancel()
				target.cancel()
				return
			}

			guard (try? await target.write(chunk)) != nil else {
				source.cancel()
				target.cancel()
				return
			}
		}
	}
}

struct RelayError: Error, CustomStringConvertible {
	let description: String

	init(_ description: String) {
		self.description = description
	}
}

// A CONNECT destination as SOCKS5 encodes it.
private struct Destination {
	enum Host {
		case ipv4([UInt8])
		case name(String)
		case ipv6([UInt8])
	}

	let host: Host
	let port: UInt16

	var portBytes: [UInt8] {
		[UInt8(port >> 8), UInt8(port & 0xFF)]
	}

	var socks5Address: [UInt8] {
		switch host {
		case let .ipv4(bytes): [0x01] + bytes
		case let .name(name): [0x03, UInt8(min(name.utf8.count, 255))] + Array(name.utf8.prefix(255))
		case let .ipv6(bytes): [0x04] + bytes
		}
	}

	static func read(type: UInt8, from connection: NWConnection) async throws -> Destination {
		let host: Host

		switch type {
		case 0x01:
			host = .ipv4(try await connection.read(4))
		case 0x03:
			let length = try await connection.read(1)[0]
			host = .name(String(decoding: try await connection.read(Int(length)), as: UTF8.self))
		case 0x04:
			host = .ipv6(try await connection.read(16))
		default:
			throw RelayError("unknown address type \(type)")
		}

		let port = try await connection.read(2)

		return Destination(host: host, port: UInt16(port[0]) << 8 | UInt16(port[1]))
	}
}

extension NWConnection {
	// Starts the connection and waits until it is ready.
	func open(on queue: DispatchQueue, timeout: TimeInterval? = nil) async throws {
		try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
			let once = Once()

			stateUpdateHandler = { state in
				switch state {
				case .ready:
					once.run { continuation.resume() }
				case let .failed(error), let .waiting(error):
					once.run { continuation.resume(throwing: error) }
				case .cancelled:
					once.run { continuation.resume(throwing: RelayError("cancelled")) }
				default:
					break
				}
			}

			start(queue: queue)

			if let timeout {
				queue.asyncAfter(deadline: .now() + timeout) { [weak self] in
					once.run {
						self?.cancel()
						continuation.resume(throwing: RelayError("timed out"))
					}
				}
			}
		}
	}

	// Exactly `count` bytes.
	func read(_ count: Int) async throws -> [UInt8] {
		guard count > 0 else {
			return []
		}

		return try await withCheckedThrowingContinuation { continuation in
			receive(minimumIncompleteLength: count, maximumLength: count) { data, _, _, error in
				if let error {
					continuation.resume(throwing: error)
				} else if let data, data.count == count {
					continuation.resume(returning: Array(data))
				} else {
					continuation.resume(throwing: RelayError("the connection closed"))
				}
			}
		}
	}

	// Whatever has arrived; empty once the other side closed.
	func readSome() async throws -> [UInt8] {
		try await withCheckedThrowingContinuation { continuation in
			receive(minimumIncompleteLength: 1, maximumLength: 65536) { data, _, isComplete, error in
				if let error {
					continuation.resume(throwing: error)
				} else if let data, !data.isEmpty {
					continuation.resume(returning: Array(data))
				} else if isComplete {
					continuation.resume(returning: [])
				} else {
					continuation.resume(returning: [])
				}
			}
		}
	}

	func write(_ bytes: [UInt8]) async throws {
		try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
			send(content: Data(bytes), completion: .contentProcessed { error in
				if let error {
					continuation.resume(throwing: error)
				} else {
					continuation.resume()
				}
			})
		}
	}
}

// Runs a closure at most once, from any thread.
final class Once: @unchecked Sendable {
	private let lock = NSLock()
	private var done = false

	func run(_ body: () -> Void) {
		lock.lock()
		let first = !done
		done = true
		lock.unlock()

		if first {
			body()
		}
	}
}
