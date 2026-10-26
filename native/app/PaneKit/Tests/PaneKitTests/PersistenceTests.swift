// Settings cases ported from apps/veil/src/stores/settings-store.test.ts, plus
// round trips through the Electron app's file formats.
import Foundation
import PaneKit
import Testing

private func temporaryDirectory() throws -> URL {
	let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
	try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)

	return url
}

@Suite("settings persistence")
struct SettingsPersistenceTests {
	@Test("fills defaults for fields a persisted settings.json predates")
	func fillsDefaults() throws {
		let directory = try temporaryDirectory()
		try #"{"state":{"settings":{"chromiumPath":"/Applications/Chromium.app"}},"version":0}"#
			.write(to: directory.appendingPathComponent("settings.json"), atomically: true, encoding: .utf8)

		let settings = try PersistedFile<SettingsState>(directory: directory, name: "settings").load()?.settings

		#expect(settings == AppSettings(
			chromiumPath: "/Applications/Chromium.app",
			theme: .system,
			tabSleepAfterMinutes: 15,
			profileUnloadAfterMinutes: 30,
			cacheBudgetMB: 300
		))
	}

	@Test("restores a persisted theme")
	func restoresTheme() throws {
		let directory = try temporaryDirectory()
		try #"{"state":{"settings":{"chromiumPath":"","theme":"dark"}},"version":0}"#
			.write(to: directory.appendingPathComponent("settings.json"), atomically: true, encoding: .utf8)

		let settings = try PersistedFile<SettingsState>(directory: directory, name: "settings").load()?.settings

		#expect(settings?.theme == .dark)
	}

	@Test("keeps an explicit null as off")
	func explicitNullIsOff() throws {
		let directory = try temporaryDirectory()
		try #"{"state":{"settings":{"tabSleepAfterMinutes":null}},"version":0}"#
			.write(to: directory.appendingPathComponent("settings.json"), atomically: true, encoding: .utf8)

		let settings = try PersistedFile<SettingsState>(directory: directory, name: "settings").load()?.settings

		#expect(settings?.tabSleepAfterMinutes == nil)
		#expect(settings?.profileUnloadAfterMinutes == 30)
	}
}

@Suite("profiles persistence")
struct ProfilesPersistenceTests {
	// The shape the Electron app writes: defaults absent from older files,
	// explicit nulls, tabs without isLoaded.
	private let electronProfiles = #"""
	{"state":{"profiles":[{"name":"Work","group":null,"fingerprint":{"userAgent":"UA","platform":"MACOS","screen":{"width":1512,"height":982},"language":"en-US","languages":["en-US","en"],"timezone":"America/Sao_Paulo","webgl":null,"hardwareConcurrency":8,"deviceMemory":8,"maxTouchPoints":0},"proxy":{"proxyType":"SOCKS5","host":"127.0.0.1","port":1080,"username":"u","password":null},"id":"p1","tabs":[{"id":"t1","url":"https://example.com/","title":"Example Domain"}],"createdAt":"2026-01-01T00:00:00.000Z","updatedAt":"2026-01-02T00:00:00.000Z"}]},"version":0}
	"""#

	@Test("reads the Electron app's profiles.json with its defaults")
	func readsElectronFile() throws {
		let directory = try temporaryDirectory()
		try electronProfiles.write(to: directory.appendingPathComponent("profiles.json"), atomically: true, encoding: .utf8)

		let profile = try #require(PersistedFile<ProfilesState>(directory: directory, name: "profiles").load()?.profiles.first)

		#expect(profile.color == .blue)
		#expect(profile.keepLoaded == false)
		#expect(profile.group == nil)
		#expect(profile.tabs == [Tab(id: "t1", url: "https://example.com/", title: "Example Domain", favicon: "")])
		#expect(profile.fingerprint?.screen.colorDepth == 24)
		#expect(profile.fingerprint?.canvas.noise == true)
		#expect(profile.fingerprint?.audio.noise == true)
		#expect(profile.proxy == ProxyConfig(proxyType: .socks5, host: "127.0.0.1", port: 1080, username: "u", password: nil))
	}

	@Test("round-trips through save and load")
	func roundTrips() throws {
		let directory = try temporaryDirectory()
		let file = PersistedFile<ProfilesState>(directory: directory, name: "profiles")
		let state = ProfilesState(profiles: [
			BrowserProfile(id: "p1", name: "Work", color: .violet, group: "Clients", keepLoaded: true, tabs: [Tab(id: "t1", url: "https://example.com/")], createdAt: "a", updatedAt: "b"),
		])

		try file.save(state)

		#expect(try file.load() == state)
	}

	@Test("writes explicit nulls, as the Electron app does")
	func writesNulls() throws {
		let directory = try temporaryDirectory()
		let file = PersistedFile<ProfilesState>(directory: directory, name: "profiles")
		try file.save(ProfilesState(profiles: [BrowserProfile(id: "p1", name: "Work", createdAt: "a", updatedAt: "b")]))

		let json = try String(contentsOf: file.url, encoding: .utf8)

		#expect(json.contains(#""group":null"#))
		#expect(json.contains(#""proxy":null"#))
		#expect(json.contains(#""fingerprint":null"#))
		#expect(json.contains(#""version":0"#))
	}

	@Test("reports a missing file as nil")
	func missingFile() throws {
		#expect(try PersistedFile<ProfilesState>(directory: temporaryDirectory(), name: "profiles").load() == nil)
	}
}

@Suite("TabHistory")
struct TabHistoryTests {
	@Test("moves a used tab to the front once")
	func use() {
		var history = TabHistory()
		history.use("a")
		history.use("b")
		history.use("a")

		#expect(history.mru == ["a", "b"])
	}

	@Test("forgets a closed tab")
	func forget() {
		var history = TabHistory()
		history.use("a")
		history.use("b")
		history.forget("a")

		#expect(history.mru == ["b"])
	}

	@Test("keeps the 20 most recently closed tabs, newest first")
	func closedStack() {
		var history = TabHistory()

		for index in 0 ..< 25 {
			history.pushClosed(ClosedTab(url: "https://\(index).test/", profileID: "p"))
		}

		#expect(history.closed.count == 20)
		#expect(history.popClosed()?.url == "https://24.test/")
		#expect(history.closed.count == 19)
	}

	@Test("pops nothing from an empty stack")
	func emptyPop() {
		var history = TabHistory()

		#expect(history.popClosed() == nil)
	}
}
