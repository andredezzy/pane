// Settings cases ported from apps/veil/src/stores/settings-store.test.ts, and
// round trips through the files' formats.
import Foundation
import PaneKit
import Testing

private func temporaryDirectory() throws -> URL {
	let url = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString, isDirectory: true)
	try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)

	return url
}

private func remove(_ directory: URL) {
	try? FileManager.default.removeItem(at: directory)
}

@Suite("settings persistence")
struct SettingsPersistenceTests {
	@Test("fills defaults for fields a persisted settings.json predates")
	func fillsDefaults() throws {
		let directory = try temporaryDirectory()
		defer { remove(directory) }
		try #"{"state":{"settings":{"theme":"system"}},"version":0}"#
			.write(to: directory.appendingPathComponent("settings.json"), atomically: true, encoding: .utf8)

		let settings = try PersistedFile<SettingsState>(directory: directory, name: "settings").load()?.settings

		#expect(settings == AppSettings(
			theme: .system,
			tabSleepAfterMinutes: 15,
			profileUnloadAfterMinutes: 30,
			cacheBudgetMB: 300
		))
	}

	@Test("restores a persisted theme")
	func restoresTheme() throws {
		let directory = try temporaryDirectory()
		defer { remove(directory) }
		try #"{"state":{"settings":{"theme":"dark"}},"version":0}"#
			.write(to: directory.appendingPathComponent("settings.json"), atomically: true, encoding: .utf8)

		let settings = try PersistedFile<SettingsState>(directory: directory, name: "settings").load()?.settings

		#expect(settings?.theme == .dark)
	}

	@Test("restores a persisted app icon, automatic by default")
	func restoresAppIcon() throws {
		let directory = try temporaryDirectory()
		defer { remove(directory) }
		let file = PersistedFile<SettingsState>(directory: directory, name: "settings")

		#expect(AppSettings.defaults.appIcon == .automatic)

		try #"{"state":{"settings":{"appIcon":"dusk"}},"version":0}"#
			.write(to: directory.appendingPathComponent("settings.json"), atomically: true, encoding: .utf8)
		#expect(try file.load()?.settings.appIcon == .dusk)

		try file.save(SettingsState(settings: AppSettings(appIcon: .moonrise)))
		#expect(try file.load()?.settings.appIcon == .moonrise)
	}

	@Test("shows Day under a light appearance and Night under a dark one when automatic")
	func automaticAppIcon() {
		#expect(AppIcon.automatic.resolved(dark: false) == .day)
		#expect(AppIcon.automatic.resolved(dark: true) == .night)
		#expect(AppIcon.dusk.resolved(dark: false) == .dusk)
		#expect(AppIcon.day.resolved(dark: true) == .day)

		// Every other choice is its own window, whatever the appearance.
		for icon in AppIcon.allCases where icon != .automatic {
			#expect(icon.resolved(dark: true) == icon && icon.resolved(dark: false) == icon)
		}
	}

	@Test("locks after 15 minutes without use by default, and keeps Never")
	func lockAfter() throws {
		let directory = try temporaryDirectory()
		defer { remove(directory) }
		let file = PersistedFile<SettingsState>(directory: directory, name: "settings")

		// A settings.json from before ADR 0116.
		try #"{"state":{"settings":{"theme":"dark"}},"version":0}"#
			.write(to: directory.appendingPathComponent("settings.json"), atomically: true, encoding: .utf8)
		#expect(try file.load()?.settings.lockAfterMinutes == 15)

		try file.save(SettingsState(settings: AppSettings(lockAfterMinutes: nil)))
		#expect(try file.load()?.settings.lockAfterMinutes == nil)

		try file.save(SettingsState(settings: AppSettings(lockAfterMinutes: 1)))
		#expect(try file.load()?.settings.lockAfterMinutes == 1)
	}

	@Test("keeps an explicit null as off")
	func explicitNullIsOff() throws {
		let directory = try temporaryDirectory()
		defer { remove(directory) }
		try #"{"state":{"settings":{"tabSleepAfterMinutes":null}},"version":0}"#
			.write(to: directory.appendingPathComponent("settings.json"), atomically: true, encoding: .utf8)

		let settings = try PersistedFile<SettingsState>(directory: directory, name: "settings").load()?.settings

		#expect(settings?.tabSleepAfterMinutes == nil)
		#expect(settings?.profileUnloadAfterMinutes == 30)
	}

	@Test("keeps the sidebar's width, within its range")
	func sidebarWidth() throws {
		let directory = try temporaryDirectory()
		defer { remove(directory) }
		let file = PersistedFile<SettingsState>(directory: directory, name: "settings")

		#expect(AppSettings.defaults.sidebarWidth == 260)

		try file.save(SettingsState(settings: AppSettings(sidebarWidth: 312)))
		#expect(try file.load()?.settings.sidebarWidth == 312)

		try #"{"state":{"settings":{"sidebarWidth":900}},"version":0}"#
			.write(to: directory.appendingPathComponent("settings.json"), atomically: true, encoding: .utf8)
		#expect(try file.load()?.settings.sidebarWidth == 400)
		#expect(SidebarWidth.clamped(120) == 200)
	}
}

@Suite("profiles persistence")
struct ProfilesPersistenceTests {
	// A file that misses a key every Pane since 0.14 wrote fails whole: ModelStore
	// keeps it aside (ADR 0122).
	@Test("a profile missing a key fails its whole file")
	func missingKey() throws {
		let directory = try temporaryDirectory()
		defer { remove(directory) }
		let file = PersistedFile<ProfilesState>(directory: directory, name: "profiles")
		try file.save(ProfilesState(profiles: [BrowserProfile(id: "p1", name: "Work", createdAt: "a", updatedAt: "b")]))

		let whole = try String(contentsOf: file.url, encoding: .utf8)
		try whole.replacingOccurrences(of: #""keepLoaded":false,"#, with: "").write(to: file.url, atomically: true, encoding: .utf8)

		#expect(whole.contains(#""keepLoaded":false,"#))
		#expect(throws: DecodingError.self) { try file.load() }
	}

	// The floor rewrites the file only when something changes, so one it opened and
	// left is still 0.15's (ADR 0122). data-volume goes through it for real.
	@Test("what a file the floor left unsaved can miss still reads")
	func fileTheFloorLeftUnsaved() throws {
		let object = { (saves: Bool?, owed: Bool?, zone: String?) throws -> Data in
			var fingerprint = Fingerprint.preset(.macOS)
			fingerprint.timezone = zone ?? ""
			var profile = try #require(JSONSerialization.jsonObject(with: JSONEncoder().encode(BrowserProfile(id: "p1", name: "Work", fingerprint: fingerprint, createdAt: "a", updatedAt: "b"))) as? [String: Any])
			var encoded = try #require(profile["fingerprint"] as? [String: Any])

			encoded["timeZone"] = zone
			encoded["timezone"] = "America/Sao_Paulo"
			profile["fingerprint"] = encoded
			profile["savesHistory"] = saves
			profile["historyClearOwed"] = owed

			return try JSONSerialization.data(withJSONObject: profile)
		}

		let decode = { (data: Data) throws in try JSONDecoder().decode(BrowserProfile.self, from: data) }
		let old = try decode(object(nil, nil, nil))

		#expect(old.savesHistory)
		#expect(!old.clearsHistory)
		// Its zone was a preset's, under the older key: it follows the computer.
		#expect(old.fingerprint?.timezone == "")

		// One that is off owes a clear, for what those versions neither kept off nor
		// cleared.
		#expect(try decode(object(false, nil, nil)).clearsHistory)
		#expect(try !decode(object(false, false, nil)).clearsHistory)
		#expect(try decode(object(nil, nil, "Asia/Tokyo")).fingerprint?.timezone == "Asia/Tokyo")
	}

	@Test("round-trips through save and load")
	func roundTrips() throws {
		let directory = try temporaryDirectory()
		defer { remove(directory) }
		let file = PersistedFile<ProfilesState>(directory: directory, name: "profiles")
		let state = ProfilesState(profiles: [
			BrowserProfile(id: "p1", name: "Work", color: .violet, group: "Clients", keepLoaded: true, savesHistory: false, tabs: [Tab(id: "t1", url: "https://example.com/")], createdAt: "a", updatedAt: "b"),
		])

		try file.save(state)

		#expect(try file.load() == state)
	}

	@Test("a zone chosen outlives a quit")
	func chosenTimeZone() throws {
		let directory = try temporaryDirectory()
		defer { remove(directory) }
		let file = PersistedFile<ProfilesState>(directory: directory, name: "profiles")
		var fingerprint = Fingerprint.preset(.macOS)
		fingerprint.timezone = "Asia/Tokyo"

		try file.save(ProfilesState(profiles: [BrowserProfile(id: "p1", name: "Work", fingerprint: fingerprint, createdAt: "a", updatedAt: "b")]))

		#expect(try file.load()?.profiles.first?.fingerprint?.timezone == "Asia/Tokyo")

		let json = try String(contentsOf: file.url, encoding: .utf8)
		#expect(json.contains(#""timeZone":"Asia\/Tokyo""#))
	}

	@Test("a clear still owed outlives a quit")
	func clearOwedPersists() throws {
		let directory = try temporaryDirectory()
		defer { remove(directory) }
		let file = PersistedFile<ProfilesState>(directory: directory, name: "profiles")
		var profile = BrowserProfile(id: "p1", name: "Work", createdAt: "a", updatedAt: "b")
		profile.savesHistory = false

		try file.save(ProfilesState(profiles: [profile]))

		#expect(try file.load()?.profiles.first?.clearsHistory == true)
	}

	@Test("reports a missing file as nil")
	func missingFile() throws {
		let directory = try temporaryDirectory()
		defer { remove(directory) }

		#expect(try PersistedFile<ProfilesState>(directory: directory, name: "profiles").load() == nil)
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
