import Foundation
import PaneCore
import PaneKit

// Each profile's network, as the Electron app set up a session (apps/veil
// profile/profile.ts): its proxy as Chrome's fixed rule, through a local relay
// for SOCKS with credentials; the WebRTC policy that keeps the real address
// from leaking past it; and the proxy's connection test for the sidebar.
@MainActor
final class ProfileNetwork {
	private struct Relay {
		let config: ProxyConfig
		let relay: SocksRelay
		let port: Int
	}

	private let model: BrowserModel
	private var relays: [String: Relay] = [:]
	// When each profile's proxy was last answered automatically, to tell
	// wrong credentials (asked again at once) from a new challenge.
	private var lastAnswer: [String: Date] = [:]

	init(model: BrowserModel) {
		self.model = model
	}

	// Applies the profile's proxy to its context, before any of its pages
	// loads. A proxy changed while its relay started (the profile sheet saved
	// as the profile opened) is applied in its place, never dropped: two
	// applies running at once both end on the profile's current proxy.
	func apply(to context: BrowserContext) async {
		let profileID = context.identifier
		var proxy: ProxyConfig?
		var rules: String?

		repeat {
			proxy = model.profile(profileID)?.proxy
			rules = nil

			if let proxy {
				rules = proxy.rules(relayPort: await relayPort(for: proxy, profileID: profileID))
			} else {
				stopRelay(profileID)
			}
		} while model.profile(profileID)?.proxy != proxy

		context.proxyRules = rules
		context.webRTCIPHandlingPolicy = WebRTCPolicy.forProfile(proxied: proxy != nil)
	}

	func stop(_ profileID: String) {
		stopRelay(profileID)
		lastAnswer[profileID] = nil
	}

	// The profile's proxy credentials for a challenge from its proxy, once:
	// asked again within a few seconds, the credentials were wrong, and the
	// user is asked instead.
	func credentials(for challenge: AuthenticationChallenge, profileID: String) -> (username: String, password: String)? {
		guard
			challenge.isForProxy,
			let proxy = model.profile(profileID)?.proxy,
			let username = proxy.username, !username.isEmpty,
			proxy.isChallenger(challenge.host)
		else {
			return nil
		}

		if let last = lastAnswer[profileID], Date().timeIntervalSince(last) < 5 {
			lastAnswer[profileID] = nil
			return nil
		}

		lastAnswer[profileID] = Date()

		return (username, proxy.password ?? "")
	}

	// Tests the profile's proxy and records the outcome for the sidebar.
	func test(_ profileID: String) {
		guard let proxy = model.profile(profileID)?.proxy else {
			model.setProxyStatus(profileID, nil)
			return
		}

		model.setProxyStatus(profileID, .testing)

		Task { [weak self] in
			let outcome = await ProxyTest.run(proxy)

			// A newer proxy is being tested by then.
			guard let self, model.profile(profileID)?.proxy == proxy else {
				return
			}

			switch outcome {
			case .connected:
				model.setProxyStatus(profileID, .connected)
			case let .failed(error):
				NSLog("Pane: the proxy of profile %@ failed its test: %@", profileID, error)
				model.setProxyStatus(profileID, .failed)
			}
		}
	}

	private func relayPort(for proxy: ProxyConfig, profileID: String) async -> Int? {
		guard proxy.needsRelay else {
			stopRelay(profileID)
			return nil
		}

		if let current = relays[profileID], current.config == proxy {
			return current.port
		}

		stopRelay(profileID)

		do {
			let relay = try SocksRelay(upstream: proxy)
			let port = try await relay.start()
			relays[profileID] = Relay(config: proxy, relay: relay, port: port)

			return port
		} catch {
			NSLog("Pane: could not start the SOCKS relay of profile %@: %@", profileID, String(describing: error))
			return nil
		}
	}

	private func stopRelay(_ profileID: String) {
		relays.removeValue(forKey: profileID)?.relay.stop()
	}
}
