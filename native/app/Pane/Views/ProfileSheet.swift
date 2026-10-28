import PaneKit
import SwiftUI

// Create or edit a profile, as the Electron app's sheet: a 360px panel inset
// 8px from the window's top, right, and bottom, with the title and a close X,
// the form (name, group, color, fingerprint, proxy), and Cancel / Create.
struct ProfileSheet: View {
	let controller: BrowserController
	let profileID: String?
	let onDone: () -> Void

	@State private var name = ""
	@State private var color: ProfileColor = .blue
	@State private var group = ""
	@State private var nameError: String?
	@State private var portError: String?
	@State private var spoofing = true
	@State private var platform = Self.hostPlatform
	@State private var proxyEnabled = false
	@State private var proxyType = ProxyType.http
	@State private var proxyHost = ""
	@State private var proxyPort = "8080"
	@State private var proxyUsername = ""
	@State private var proxyPassword = ""
	@State private var proxyShortcut = ""
	@State private var proxyTest = ProxyTestState.untested

	// Pane runs on macOS only: the host's platform.
	private static let hostPlatform = Platform.macOS

	private var model: BrowserModel { controller.model }
	private var trimmedName: String { name.trimmingCharacters(in: .whitespaces) }

	var body: some View {
		VStack(spacing: 0) {
			header
			hairline

			ScrollView {
				VStack(alignment: .leading, spacing: 16) {
					VStack(alignment: .leading, spacing: 0) {
						FormLabel(title: "Name")
						PaneTextField(placeholder: "e.g. Work account", text: $name, focusesOnAppear: true, onSubmit: save)
							.accessibilityIdentifier("pane.profile.name")

						if let nameError {
							Text(nameError)
								.font(.system(size: 12.8, weight: .medium))
								.foregroundStyle(Palette.destructive)
						}
					}

					VStack(alignment: .leading, spacing: 0) {
						FormLabel(title: "Group", note: "(optional)")
						PaneTextField(placeholder: "Enter group name", text: $group, onSubmit: save)
					}

					VStack(alignment: .leading, spacing: 0) {
						FormLabel(title: "Color")
						SwatchPicker(selection: $color)
					}

					switchRow("Spoof fingerprint", isOn: $spoofing)

					if spoofing {
						platformPicker
					}

					PaneSeparator()

					switchRow("Enable proxy", isOn: $proxyEnabled)

					if proxyEnabled {
						proxyFields
					}
				}
				.padding(20)
				.frame(maxWidth: .infinity, alignment: .leading)
			}

			hairline

			HStack(spacing: 8) {
				Spacer()
				Button("Cancel", action: onDone)
					.buttonStyle(.pane(.outline))
					.keyboardShortcut(.cancelAction)
				Button(profileID == nil ? "Create" : "Save", action: save)
					.buttonStyle(.pane(.primary))
					.keyboardShortcut(.defaultAction)
					.accessibilityIdentifier("pane.profile.save")
			}
			.padding(.horizontal, 20)
			.padding(.vertical, 16)
		}
		// Clipped before the shadow goes on, which would clip it too.
		.clipShape(Squircle(cornerRadius: 18))
		.surfaceBackground(cornerRadius: 18, shadow: .black.opacity(0.5), blur: 40, y: 8)
		.onAppear(perform: load)
		.onChange(of: name) { nameError = nil }
		.onChange(of: proxyPort) { portError = nil }
		.onExitCommand(perform: onDone)
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.profileSheet")
	}

	private var header: some View {
		HStack {
			Text(profileID == nil ? "New profile" : "Edit profile")
				.font(.system(size: 14, weight: .medium))
				.foregroundStyle(Palette.accentForeground)
				.lineBox(20)

			Spacer()

			CloseButton(action: onDone)
		}
		.frame(height: 24)
		.padding(.horizontal, 20)
		.padding(.vertical, 16)
	}

	// The Electron app's header and footer rules: white 5%, which only shows
	// on dark.
	private var hairline: some View {
		Rectangle()
			.fill(Color.white.opacity(0.05))
			.frame(height: 1)
	}

	// A label at left, its switch at right: the switch's own label, so
	// accessibility names the switch.
	private func switchRow(_ title: String, isOn: Binding<Bool>) -> some View {
		Toggle(isOn: isOn) {
			Text(title)
				.font(.system(size: 11, weight: .medium))
				.foregroundStyle(Palette.foreground)
				.frame(maxWidth: .infinity, alignment: .leading)
		}
		.toggleStyle(.paneSwitch)
	}

	// The platform a fingerprint claims; another than the Mac's gets the
	// Electron app's warning.
	private var platformPicker: some View {
		VStack(alignment: .leading, spacing: 0) {
			FormLabel(title: "Platform")
			PresetRow(
				options: [Platform.windows, .macOS, .linux],
				selection: platform,
				// CSS capitalize of the lowercased name, as the Electron app
				// shows it.
				title: { $0.rawValue.lowercased().capitalized },
				select: { platform = $0 }
			)
			.accessibilityElement(children: .contain)
			.accessibilityIdentifier("pane.profile.platform")

			if platform != Self.hostPlatform {
				Text("Cross-OS spoofing can\u{2019}t fool strict WebGL checks \u{2014} use \(Self.hostPlatform.rawValue.lowercased()) or turn spoofing off for Cloudflare-protected sites.")
					.font(.system(size: 10))
					.foregroundStyle(Palette.mutedForeground)
					.fixedSize(horizontal: false, vertical: true)
					.padding(.top, 8)
			}
		}
	}

	// The proxy: a paste shortcut, type, host, port, credentials, and its
	// test, in a bordered box.
	private var proxyFields: some View {
		VStack(alignment: .leading, spacing: 12) {
			PaneTextField(placeholder: "host:port:username:password", text: $proxyShortcut, monospaced: true)
				.onChange(of: proxyShortcut) { pasteProxy(proxyShortcut) }
				.accessibilityIdentifier("pane.profile.proxyShortcut")

			HStack(spacing: 8) {
				PaneSelect(options: ProxyType.allCases, selection: $proxyType, title: \.rawValue)
					.frame(width: 100)
				PaneTextField(placeholder: "Host", text: $proxyHost)
					.accessibilityIdentifier("pane.profile.proxyHost")
				PaneTextField(placeholder: "Port", text: $proxyPort)
					.frame(width: 80)
					.accessibilityIdentifier("pane.profile.proxyPort")
			}

			// As the name's: a profile that asks for a proxy never saves
			// without one.
			if let portError {
				Text(portError)
					.font(.system(size: 12.8, weight: .medium))
					.foregroundStyle(Palette.destructive)
					// Right under the row, as the name's error sits under its field.
					.padding(.top, -12)
					.accessibilityIdentifier("pane.profile.portError")
			}

			HStack(spacing: 8) {
				PaneTextField(placeholder: "Username (optional)", text: $proxyUsername)
				PaneTextField(placeholder: "Password (optional)", text: $proxyPassword, secure: true)
			}

			Button(action: testProxy) {
				HStack(spacing: 8) {
					if proxyTest == .testing {
						LucideIcon(.loaderCircle, size: 12)
							.rotationEffect(.degrees(spin ? 360 : 0))
							.onAppear { withAnimation(Motion.spin) { spin = true } }
							.onDisappear { spin = false }
					}

					Text(proxyTest.label)
						.lineLimit(1)
				}
			}
			.buttonStyle(.pane(
				.outline,
				size: .small,
				fontSize: 11,
				fillsWidth: true,
				foreground: proxyTest.foreground,
				fill: proxyTest.fill,
				border: proxyTest.border
			))
			.disabled(proxyTest == .testing || proxyHost.trimmingCharacters(in: .whitespaces).isEmpty)
			.accessibilityIdentifier("pane.profile.testProxy")
		}
		.padding(12)
		.overlay {
			Squircle(cornerRadius: 11)
				.strokeBorder(Palette.border, lineWidth: 1)
		}
	}

	@State private var spin = false

	private static let portMessage = "Port must be between 1 and 65535"

	// The proxy as filled in, once it has a host and a valid port.
	private var proxyConfig: ProxyConfig? {
		let host = proxyHost.trimmingCharacters(in: .whitespaces)

		guard proxyEnabled, !host.isEmpty, let port = ProxyConfig.port(proxyPort) else {
			return nil
		}

		return ProxyConfig(
			proxyType: proxyType,
			host: host,
			port: port,
			username: proxyUsername.isEmpty ? nil : proxyUsername,
			password: proxyPassword.isEmpty ? nil : proxyPassword
		)
	}

	private func pasteProxy(_ raw: String) {
		guard let parsed = ProxyString(raw) else {
			return
		}

		proxyHost = parsed.host
		proxyPort = String(parsed.port)

		if let username = parsed.username {
			proxyUsername = username
		}

		if let password = parsed.password {
			proxyPassword = password
		}

		proxyTest = .untested
	}

	private func testProxy() {
		guard let config = proxyConfig else {
			proxyTest = .failed(Self.portMessage)
			return
		}

		proxyTest = .testing

		Task {
			switch await ProxyTest.run(config) {
			case let .connected(ip): proxyTest = .connected(ip)
			case let .failed(error): proxyTest = .failed(error)
			}
		}
	}

	private func load() {
		guard let profile = profileID.flatMap(model.profile) else {
			return
		}

		name = profile.name
		color = profile.color
		group = profile.group ?? ""
		spoofing = profile.fingerprint != nil
		platform = profile.fingerprint?.platform ?? Self.hostPlatform
		proxyEnabled = profile.proxy != nil

		if let proxy = profile.proxy {
			proxyType = proxy.proxyType
			proxyHost = proxy.host
			proxyPort = String(proxy.port)
			proxyUsername = proxy.username ?? ""
			proxyPassword = proxy.password ?? ""
		}
	}

	private func save() {
		guard !trimmedName.isEmpty else {
			nameError = "Profile name is required"
			return
		}

		// A proxy with a host but no valid port would save as no proxy, and
		// the profile would go direct.
		guard !proxyEnabled || proxyHost.trimmingCharacters(in: .whitespaces).isEmpty || ProxyConfig.port(proxyPort) != nil else {
			portError = Self.portMessage
			return
		}

		let groupValue = group.trimmingCharacters(in: .whitespaces)
		let generate = { Fingerprint.generated(for: platform, hostWebGL: controller.hostWebGL) }
		let proxy = proxyConfig

		if let profileID {
			let stored = model.profile(profileID)?.fingerprint

			model.updateProfile(profileID) {
				$0.name = trimmedName
				$0.color = color
				$0.group = groupValue.isEmpty ? nil : groupValue
				$0.fingerprint = Fingerprint.edited(stored: stored, spoofing: spoofing, platform: platform, generate: generate)
				$0.proxy = proxy
			}
		} else {
			let id = model.createProfile(name: trimmedName, color: color, group: groupValue.isEmpty ? nil : groupValue)
			let fingerprint = spoofing ? generate() : nil

			model.updateProfile(id) {
				$0.fingerprint = fingerprint
				$0.proxy = proxy
			}
			model.expandedProfileIDs.insert(id)
		}

		onDone()
	}
}

// 24px, rounded 7: a 16px X, muted until hovered.
struct CloseButton: View {
	let action: () -> Void

	@State private var isHovered = false

	var body: some View {
		Button(action: action) {
			LucideIcon(.x, size: 16)
				.frame(width: 24, height: 24)
				.contentShape(Rectangle())
		}
		.buttonStyle(.plain)
		.foregroundStyle(isHovered ? Palette.foreground : Palette.mutedForeground)
		.onHover { hovering in
			withAnimation(Motion.hover) { isHovered = hovering }
		}
		.accessibilityLabel("Close")
	}
}

// 24px swatches, rounded-md, 6px apart; the selected one is ringed with the
// background then its color.
private struct SwatchPicker: View {
	@Binding var selection: ProfileColor

	var body: some View {
		HStack(spacing: 6) {
			ForEach(ProfileColor.allCases, id: \.self) { swatch in
				let isSelected = swatch == selection

				Button { selection = swatch } label: {
					Squircle(cornerRadius: 11)
						.fill(swatch.color)
						.frame(width: 24, height: 24)
						.background {
							if isSelected {
								Squircle(cornerRadius: 17)
									.fill(swatch.color)
									.padding(-3)
								Squircle(cornerRadius: 15)
									.fill(Palette.background)
									.padding(-2)
							}
						}
				}
				.buttonStyle(.plain)
				.accessibilityLabel(swatch.rawValue.capitalized)
				.accessibilityAddTraits(isSelected ? .isSelected : [])
			}
		}
	}
}

// The proxy test button's states (profile-sheet.tsx): untested, running, the
// proxy's IP on a green button, or the error in red.
private enum ProxyTestState: Equatable {
	case untested
	case testing
	case connected(String)
	case failed(String)

	var label: String {
		switch self {
		case .untested, .testing: "Test proxy"
		case let .connected(ip): ip
		case let .failed(error): error
		}
	}

	var foreground: Color? {
		switch self {
		case .connected: .white
		case .failed: Palette.destructive
		default: nil
		}
	}

	// emerald-600 for a working proxy.
	var fill: Color? {
		if case .connected = self {
			return Color(rgb: 0x059669)
		}

		return nil
	}

	var border: Color? {
		switch self {
		case .connected: Color(rgb: 0x059669)
		case .failed: Palette.destructive
		default: nil
		}
	}
}
