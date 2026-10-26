import Foundation
import Network
import PaneKit

// A proxy's connection test (apps/veil profile/proxy-test.ts): an IP echo
// fetched through the proxy, as Chrome will reach it (SOCKS through the
// relay; it also speaks SOCKS4, which URLSession does not), within 10s.
enum ProxyTest {
	enum Outcome: Equatable, Sendable {
		case connected(ip: String)
		case failed(String)
	}

	// The Electron app's echo; scenarios point it at their own.
	private static var testURL: URL {
		ProcessInfo.processInfo.environment["PANE_PROXY_TEST_URL"].flatMap(URL.init(string:))
			?? URL(string: "https://httpbin.org/ip")!
	}

	static func run(_ config: ProxyConfig) async -> Outcome {
		var relay: SocksRelay?
		defer { relay?.stop() }

		do {
			let proxy: ProxyConfiguration

			switch config.proxyType {
			case .http:
				proxy = ProxyConfiguration(httpCONNECTProxy: try endpoint(config.host, config.port))
			case .https:
				proxy = ProxyConfiguration(httpCONNECTProxy: try endpoint(config.host, config.port), tlsOptions: NWProtocolTLS.Options())
			case .socks5 where !config.needsRelay:
				proxy = ProxyConfiguration(socksv5Proxy: try endpoint(config.host, config.port))
			case .socks4, .socks5:
				let local = try SocksRelay(upstream: config)
				relay = local
				proxy = ProxyConfiguration(socksv5Proxy: try endpoint("127.0.0.1", try await local.start()))
			}

			if config.proxyType == .http || config.proxyType == .https, let username = config.username, !username.isEmpty {
				proxy.applyCredential(username: username, password: config.password ?? "")
			}

			let configuration = URLSessionConfiguration.ephemeral
			configuration.proxyConfigurations = [proxy]
			configuration.timeoutIntervalForRequest = 10
			configuration.timeoutIntervalForResource = 10

			let session = URLSession(configuration: configuration)
			defer { session.invalidateAndCancel() }

			let (data, response) = try await session.data(from: testURL)
			let status = (response as? HTTPURLResponse)?.statusCode ?? 0

			guard status == 200 else {
				return .failed("HTTP \(status)")
			}

			guard
				let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
			else {
				return .failed("Invalid response")
			}

			return .connected(ip: object["origin"] as? String ?? "Unknown")
		} catch let error as URLError where error.code == .timedOut {
			return .failed("Connection timed out")
		} catch {
			return .failed((error as? RelayError)?.description ?? error.localizedDescription)
		}
	}

	private static func endpoint(_ host: String, _ port: Int) throws -> NWEndpoint {
		guard let port = NWEndpoint.Port(rawValue: UInt16(clamping: port)) else {
			throw RelayError("bad port")
		}

		return .hostPort(host: NWEndpoint.Host(host), port: port)
	}
}
