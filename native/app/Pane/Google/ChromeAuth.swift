import Foundation
import PaneKit

// A real Chrome that signs in to Google when Google rejects the engine
// (apps/veil profile/google/auth-window.ts). Each sign-in has its own Chrome
// and temporary profile, so two profiles signing in at once never import
// each other's cookies. Its DevTools port is the one Chrome itself wrote to
// that profile's DevToolsActivePort: a fixed port could be taken by another
// local process serving forged cookies.
@MainActor
final class ChromeAuth {
	// A cookie as the DevTools protocol reports it.
	struct Cookie: Decodable, Sendable {
		let name: String
		let value: String
		let domain: String
		let path: String
		let secure: Bool
		let httpOnly: Bool
		// Seconds since 1970; -1 for a session cookie.
		let expires: Double
		let sameSite: String?
	}

	enum Failure: LocalizedError {
		case unreachable
		case devTools(String)
		case timedOut

		var errorDescription: String? {
			switch self {
			case .unreachable: "Could not connect to Chrome. Is the Chrome window still open?"
			case let .devTools(message): message
			case .timedOut: "Chrome did not answer in time."
			}
		}
	}

	// The Electron app's candidates, Google Chrome first.
	private static let candidates = [
		"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
		"/Applications/Chromium.app/Contents/MacOS/Chromium",
		"/Applications/Google Chrome Canary.app/Contents/MacOS/Google Chrome Canary",
	]

	let directory: URL
	private let process: Process

	// Settings' Chrome when it names one, else the first installed.
	static func chromePath(settings: AppSettings) -> String? {
		([settings.chromiumPath] + candidates).first { !$0.isEmpty && FileManager.default.isExecutableFile(atPath: $0) }
	}

	init(chromePath: String, loginURL: URL) throws {
		directory = FileManager.default.temporaryDirectory.appending(path: "pane-google-auth-\(UUID().uuidString)")
		try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)

		process = Process()
		process.executableURL = URL(fileURLWithPath: chromePath)
		process.arguments = [
			"--user-data-dir=\(directory.path)",
			"--remote-debugging-port=0",
			"--no-first-run",
			"--no-default-browser-check",
			loginURL.absoluteString,
		]
		process.standardOutput = FileHandle.nullDevice
		process.standardError = FileHandle.nullDevice

		do {
			try process.run()
		} catch {
			try? FileManager.default.removeItem(at: directory)
			throw error
		}
	}

	// The Google and YouTube cookies Chrome holds, read through its DevTools
	// endpoint (it may still be starting: up to ten seconds).
	func googleCookies() async throws -> [Cookie] {
		guard let socket = await browserSocket() else {
			throw Failure.unreachable
		}

		return try await Self.cookies(over: socket).filter { GoogleAccounts.isCookieDomain($0.domain) }
	}

	// Quits Chrome and, once it let go of its profile, deletes it.
	func stop() {
		if process.isRunning {
			process.terminate()
		}

		let directory = directory

		Task.detached {
			try? await Task.sleep(for: .seconds(2))
			try? FileManager.default.removeItem(at: directory)
		}
	}

	private func browserSocket() async -> URL? {
		for _ in 0 ..< 20 {
			if let port = devToolsPort(),
			   let (data, _) = try? await URLSession.shared.data(from: URL(string: "http://127.0.0.1:\(port)/json/version")!),
			   let version = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
			   let socket = version["webSocketDebuggerUrl"] as? String,
			   // Only a loopback socket on the port Chrome reported.
			   socket.hasPrefix("ws://127.0.0.1:\(port)/")
			{
				return URL(string: socket)
			}

			try? await Task.sleep(for: .milliseconds(500))
		}

		return nil
	}

	private func devToolsPort() -> Int? {
		guard let content = try? String(contentsOf: directory.appending(path: "DevToolsActivePort"), encoding: .utf8),
		      let port = content.split(separator: "\n").first.flatMap({ Int($0) }), port > 0
		else {
			return nil
		}

		return port
	}

	private struct Reply: Decodable {
		struct Result: Decodable {
			let cookies: [Cookie]
		}

		struct Error: Decodable {
			let message: String
		}

		let id: Int?
		let result: Result?
		let error: Error?
	}

	private nonisolated static func cookies(over socket: URL) async throws -> [Cookie] {
		let task = URLSession.shared.webSocketTask(with: socket)
		task.resume()
		defer { task.cancel(with: .goingAway, reason: nil) }

		try await task.send(.string(#"{"id":1,"method":"Storage.getCookies"}"#))

		return try await withThrowingTaskGroup(of: [Cookie].self) { group in
			group.addTask {
				while true {
					guard case let .string(text) = try await task.receive(),
					      let reply = try? JSONDecoder().decode(Reply.self, from: Data(text.utf8)),
					      reply.id == 1
					else {
						continue
					}

					if let message = reply.error?.message {
						throw Failure.devTools(message)
					}

					return reply.result?.cookies ?? []
				}
			}
			group.addTask {
				try await Task.sleep(for: .seconds(10))
				throw Failure.timedOut
			}

			let cookies = try await group.next() ?? []
			group.cancelAll()

			return cookies
		}
	}
}
