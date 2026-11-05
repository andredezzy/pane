import PaneKit
import SwiftUI

// Create or edit a profile (Paper's 05.4 and 05.5, ADR 0120): a 400-point
// panel inset 8 from the window's top, right, and bottom, with the title and a
// close X, the name, group, and color, then Privacy, Fingerprint, and Network
// in cards, and Cancel / Create.
struct ProfileSheet: View {
	// The profile being edited; nil for a new one.
	let profile: BrowserProfile?
	// Create or Save, with the form as filled in (BrowserController's
	// createProfile and saveProfile).
	let save: (ProfileDraft) -> Void
	let onDone: () -> Void

	// The form, as PaneKit checks and applies it.
	@State private var draft = ProfileDraft()
	@State private var problem: ProfileDraft.Problem?
	@State private var proxyTest = ProxyTestState.untested
	// The proxy `proxyTest` is about.
	@State private var testedProxy: ProxyConfig?

	private var isNew: Bool { profile == nil }
	private var isCrossOS: Bool { draft.platform != ProfileDraft.hostPlatform }

	var body: some View {
		VStack(spacing: 0) {
			header
			Hairline()

			ScrollView {
				VStack(alignment: .leading, spacing: 18) {
					identity

					SheetSection(title: "Privacy") {
						SheetSwitch(title: "Save history", note: historyNote, isOn: $draft.savesHistory)
					}

					SheetSection(title: "Fingerprint") {
						SheetSwitch(title: "Spoof fingerprint", note: spoofNote, isOn: $draft.spoofing)

						if draft.spoofing {
							Hairline()
							platformFields
							Hairline()
							timeZoneRow
						}
					}

					SheetSection(title: "Network") {
						SheetSwitch(title: "Proxy", note: draft.proxyEnabled ? "Its traffic goes through this proxy." : "Connects directly.", isOn: $draft.proxyEnabled)

						if draft.proxyEnabled {
							Hairline()
							proxyFields
						}
					}
				}
				.padding(20)
				.frame(maxWidth: .infinity, alignment: .leading)
				// The warning comes and goes as a tile's click, itself instant,
				// crosses to or from the Mac's platform.
				.animation(Motion.toggle, value: isCrossOS)
			}

			Hairline()

			HStack(spacing: 8) {
				Spacer()
				Button("Cancel", action: onDone)
					.buttonStyle(.pane(.outline))
					.keyboardShortcut(.cancelAction)
				Button(isNew ? "Create" : "Save", action: submit)
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
		.onAppear { draft = profile.map(ProfileDraft.init) ?? ProfileDraft() }
		.onChange(of: draft.name) { problem = problem == .nameMissing ? nil : problem }
		.onChange(of: draft.proxyPort) { problem = problem == .portInvalid ? nil : problem }
		// A test's answer is about the proxy it tested, not the one typed since.
		.onChange(of: draft.proxy) {
			if draft.proxy != testedProxy {
				proxyTest = .untested
			}
		}
		.onExitCommand(perform: onDone)
		.accessibilityElement(children: .contain)
		// VoiceOver stays in the sheet while it shows, as in Quit Pane?.
		.accessibilityAddTraits(.isModal)
		.accessibilityLabel(isNew ? "New profile" : "Edit profile")
		.accessibilityIdentifier("pane.profileSheet")
	}

	private var header: some View {
		HStack {
			Text(isNew ? "New profile" : "Edit profile")
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

	private var identity: some View {
		VStack(alignment: .leading, spacing: 16) {
			VStack(alignment: .leading, spacing: 0) {
				LabeledField(title: "Name", labelSize: .regular) {
					PaneTextField(placeholder: "e.g. Work account", text: $draft.name, fontSize: 13, focusesOnAppear: true, onSubmit: submit)
						.accessibilityIdentifier("pane.profile.name")
				}

				if problem == .nameMissing {
					problemText(.nameMissing)
						// 8 under the field, as space-y-2 sets it.
						.padding(.top, 8)
				}
			}

			LabeledField(title: "Group", note: "(optional)", labelSize: .regular) {
				PaneTextField(placeholder: "Enter group name", text: $draft.group, fontSize: 13, onSubmit: submit)
			}

			VStack(alignment: .leading, spacing: 0) {
				FormLabel(title: "Color", size: .regular)
					.accessibilityHidden(true)
				SwatchPicker(selection: $draft.color)
					.accessibilityElement(children: .contain)
					.accessibilityLabel("Color")
			}
		}
	}

	// Saving clears only what there is to clear: on a profile that kept its
	// history until this edit.
	private var historyNote: String {
		if draft.savesHistory {
			return "Keeps the pages it visits and its downloads."
		}

		return profile?.savesHistory == true ? "Keeps no pages or downloads. Saving clears what it kept." : "Keeps no pages or downloads."
	}

	private var spoofNote: String {
		guard draft.spoofing else {
			return "Sites see this Mac."
		}

		switch draft.platform {
		case .macOS: return "Sites see a generic Mac, not this one."
		case .windows: return "Sites see a generic Windows PC, not this Mac."
		case .linux: return "Sites see a generic Linux PC, not this Mac."
		}
	}

	// The platform a fingerprint claims; another than the Mac's gets a warning.
	private var platformFields: some View {
		VStack(alignment: .leading, spacing: 10) {
			HStack(spacing: 8) {
				ForEach(Platform.allCases, id: \.self) { platform in
					Button(platform.title) {
						Log.interaction("Choose platform")
						draft.platform = platform
					}
						.buttonStyle(PlatformTileStyle(platform: platform, isSelected: platform == draft.platform))
						.accessibilityAddTraits(platform == draft.platform ? .isSelected : [])
				}
			}
			.accessibilityElement(children: .contain)
			.accessibilityLabel("Platform")
			.accessibilityIdentifier("pane.profile.platform")

			if isCrossOS {
				HStack(alignment: .top, spacing: 8) {
					LucideIcon(.triangleAlert, size: 14)
						.foregroundStyle(Palette.warningIcon)
						.padding(.top, 1)

					Text("Strict checks like Cloudflare\u{2019}s can spot a \(draft.platform.title) fingerprint on a Mac. Choose \(ProfileDraft.hostPlatform.title) for those sites.")
						.font(.system(size: 12))
						// 16-point lines.
						.lineSpacing(1)
						.padding(.vertical, 0.5)
						.foregroundStyle(Palette.accentForeground)
						.fixedSize(horizontal: false, vertical: true)
				}
				.padding(.vertical, 8)
				.padding(.horizontal, 10)
				.frame(maxWidth: .infinity, alignment: .leading)
				.background(Palette.warningFill, in: Squircle(cornerRadius: 9))
			}
		}
		.sheetRowPadding()
	}

	// The zone the fingerprint claims, or the computer's (ADR 0038).
	private var timeZoneRow: some View {
		HStack(spacing: 16) {
			RowLabels(
				title: "Time zone",
				description: draft.timeZone.isEmpty
					? "Follows this Mac: \(TimeZones.city(TimeZone.current.identifier))."
					: "Overrides this Mac\u{2019}s time zone."
			)

			TimeZoneMenu(selection: $draft.timeZone)
		}
		.sheetRowPadding()
	}

	// The proxy: its address, credentials, and test.
	private var proxyFields: some View {
		VStack(alignment: .leading, spacing: 8) {
			ProxyAddressField(draft: $draft)

			// As the name's: a profile that asks for a proxy never saves
			// without one.
			if problem == .portInvalid {
				problemText(.portInvalid)
					.accessibilityIdentifier("pane.profile.portError")
			}

			HStack(spacing: 8) {
				PaneTextField(placeholder: "Username (optional)", text: $draft.proxyUsername, fontSize: 13, fill: Palette.sheetCardField)
					.accessibilityLabel("Proxy username")
				PaneTextField(placeholder: "Password (optional)", text: $draft.proxyPassword, fontSize: 13, fill: Palette.sheetCardField, secure: true)
					.accessibilityLabel("Proxy password")
			}

			Text("Paste host:port:user:password to fill them all.")
				.font(.system(size: 12))
				.foregroundStyle(Palette.mutedForeground)
				.lineBox(16)

			HStack(spacing: 10) {
				Button("Test connection", action: testProxy)
					.buttonStyle(.pane(.outline, size: .compact, cornerRadius: 9))
					.background(Palette.sheetCardField, in: Squircle(cornerRadius: 9))
					.disabled(proxyTest == .testing || draft.proxyHost.trimmingCharacters(in: .whitespaces).isEmpty)
					.accessibilityIdentifier("pane.profile.testProxy")

				if let status = proxyTest.status {
					HStack(spacing: 6) {
						Circle()
							.fill(status.dot)
							.frame(width: 6, height: 6)

						Text(status.text)
							.font(.system(size: 12).monospacedDigit())
							.foregroundStyle(Palette.foreground)
							.lineSpacing(1)
							.fixedSize(horizontal: false, vertical: true)
					}
					.accessibilityElement(children: .combine)
					.accessibilityIdentifier("pane.profile.proxyStatus")
				}
			}
			.padding(.top, 4)
		}
		.sheetRowPadding(bottom: 14)
	}

	private func problemText(_ problem: ProfileDraft.Problem) -> some View {
		Text(problem.message)
			.font(.system(size: 12.8, weight: .medium))
			.foregroundStyle(Palette.destructive)
	}

	private func testProxy() {
		draft.pasteProxy(draft.proxyHost)

		guard let config = draft.proxy else {
			testedProxy = nil
			proxyTest = .failed(ProfileDraft.Problem.portMessage)
			return
		}

		testedProxy = config
		proxyTest = .testing

		Task {
			let result = await ProxyTest.run(config)

			guard draft.proxy == config else {
				return
			}

			switch result {
			case let .connected(ip): proxyTest = .connected(ip)
			case let .failed(error): proxyTest = .failed(error)
			}
		}
	}

	private func submit() {
		draft.pasteProxy(draft.proxyHost)
		problem = draft.validate()

		if problem == nil {
			save(draft)
			onDone()
		}
	}
}

extension View {
	// A row of a sheet's card: 15 and 14 in, inside its 1-point border.
	fileprivate func sheetRowPadding(bottom: CGFloat = 12) -> some View {
		padding(.leading, 15)
			.padding(.trailing, 14)
			.padding(.top, 12)
			.padding(.bottom, bottom)
	}
}

// A section: its muted label 15 points in, over a card rounded 15, as
// Settings' groups (ADR 0065) are at the panel's scale, its rows inside the
// 1-point border and clipped to it.
private struct SheetSection<Content: View>: View {
	let title: String
	@ViewBuilder let content: Content

	var body: some View {
		VStack(alignment: .leading, spacing: 8) {
			Text(title)
				.font(.system(size: 13))
				.foregroundStyle(Palette.mutedForeground)
				.lineBox(18)
				.padding(.leading, 15)

			VStack(alignment: .leading, spacing: 0) {
				content
			}
			.padding(1)
			.background(Palette.sheetCard)
			.clipShape(Squircle(cornerRadius: 15))
			.overlay {
				Squircle(cornerRadius: 15)
					.strokeBorder(Palette.border, lineWidth: 1)
			}
			.accessibilityElement(children: .contain)
		}
	}
}

// A switch under its title and note: the row, but for the card's margin beside
// the switch, is its button, as a label's click toggles a checkbox.
private struct SheetSwitch: View {
	let title: String
	let note: String
	@Binding var isOn: Bool

	var body: some View {
		Toggle(isOn: $isOn) {
			RowLabels(title: title, description: note)
				.padding(.vertical, 12)
				.padding(.leading, 15)
				// 16 from the switch, with the 8 its style leaves.
				.padding(.trailing, 8)
		}
		.toggleStyle(.paneSwitch)
		.padding(.trailing, 14)
		.accessibilityLabel(title)
		.accessibilityHint(note)
	}
}

// A platform: its real logo over its name, on a tile rounded 11, the chosen one
// ringed in the foreground as the App icon picker's (ADR 0089). Hover fills
// the others; choosing is instant.
private struct PlatformTileStyle: ButtonStyle {
	let platform: Platform
	let isSelected: Bool

	func makeBody(configuration: Configuration) -> some View {
		PlatformTile(configuration: configuration, platform: platform, isSelected: isSelected)
	}
}

private struct PlatformTile: View {
	let configuration: ButtonStyle.Configuration
	let platform: Platform
	let isSelected: Bool

	@State private var isHovered = false

	var body: some View {
		let shape = Squircle(cornerRadius: 11)

		VStack(spacing: 4) {
			PlatformLogo(platform: platform)
				.frame(height: 26)

			configuration.label
				.font(.system(size: 12, weight: .medium))
				.foregroundStyle(isSelected ? Palette.foreground : Palette.mutedLifted)
				.lineLimit(1)
				.lineBox(16)
		}
		.frame(maxWidth: .infinity)
		// 13 above and below, as CSS's p-3 inside the 1-point border.
		.frame(height: 72)
		.background {
			ZStack {
				shape.fill(isSelected ? Palette.chosenTile : Palette.outlineHover.opacity(isHovered ? 1 : 0))
				PressLayer(shape: shape, color: Palette.press, isPressed: configuration.isPressed)
			}
		}
		.overlay {
			shape.strokeBorder(isSelected ? Palette.foreground : Palette.border, lineWidth: isSelected ? 2 : 1)
		}
		.contentShape(shape)
		.animation(nil, value: isSelected)
		.onHover { hovering in
			withAnimation(Motion.hover) { isHovered = hovering }
		}
	}
}

// Each platform's own logo at the size Paper draws it, in its real colours.
private struct PlatformLogo: View {
	let platform: Platform

	var body: some View {
		Group {
			switch platform {
			case .windows:
				Image("PlatformWindows")
					.resizable()
					.frame(width: 22, height: 22)
			case .macOS:
				Image("PlatformApple")
					.renderingMode(.template)
					.resizable()
					.foregroundStyle(Palette.appleLogo)
					.frame(width: 21.13, height: 26)
			case .linux:
				Image("PlatformTux")
					.resizable()
					.frame(width: 22.56, height: 26)
					.shadow(color: Palette.tuxEdge, radius: 0.375)
			}
		}
		.accessibilityHidden(true)
	}
}

// Automatic, every zone by region, then UTC. The trigger shows the zone's city.
private struct TimeZoneMenu: View {
	// Listed once: their offsets change with the seasons, not while Pane runs.
	private static let regions = TimeZones.regions()

	@Binding var selection: String

	var body: some View {
		PaneMenuTrigger(
			title: selection.isEmpty ? "Automatic" : TimeZones.city(selection),
			label: "Time zone",
			chevron: .upDown
		) {
			Picker("", selection: $selection) {
				Text("Automatic").tag("")
			}
			.pickerStyle(.inline)
			.labelsHidden()

			Divider()

			ForEach(Self.regions) { region in
				Menu(region.name) {
					Picker("", selection: $selection) {
						ForEach(region.choices) { choice in
							Text(choice.title).tag(choice.identifier)
						}
					}
					.pickerStyle(.inline)
					.labelsHidden()
				}
			}

			Divider()

			Picker("", selection: $selection) {
				Text(TimeZones.utc).tag(TimeZones.utc)
			}
			.pickerStyle(.inline)
			.labelsHidden()
		}
		.accessibilityIdentifier("pane.profile.timeZone")
	}
}

// The proxy's address as one field: its type, host, and port. A proxy string
// fills them all, pasted into the host, or typed there and left (ProxyString);
// while it is typed the host stays as it is, for the colons to come.
private struct ProxyAddressField: View {
	private enum Part {
		case host
		case port
	}

	@Binding var draft: ProfileDraft

	@FocusState private var focused: Part?

	var body: some View {
		let prompt: (String) -> Text = { Text($0).foregroundStyle(Palette.mutedForeground) }

		HStack(spacing: 0) {
			PaneMenuButton(options: ProxyType.allCases.map { ($0.rawValue, $0) }, selection: draft.proxyType, label: "Proxy type", size: .embedded, chevron: .upDown) {
				draft.proxyType = $0
			}

			divider

			TextField("", text: $draft.proxyHost, prompt: prompt("Host"))
				.focused($focused, equals: .host)
				.padding(.horizontal, 12)
				.accessibilityLabel("Proxy host")
				.accessibilityIdentifier("pane.profile.proxyHost")

			divider

			TextField("", text: $draft.proxyPort, prompt: prompt("Port"))
				.focused($focused, equals: .port)
				.monospacedDigit()
				.padding(.horizontal, 12)
				// Room for 65535 at 13: 41 points, and 12 either side.
				.frame(width: 65)
				.accessibilityLabel("Proxy port")
				.accessibilityIdentifier("pane.profile.proxyPort")
		}
		.textFieldStyle(.plain)
		.font(.system(size: 13))
		.foregroundStyle(Palette.foreground)
		.clipShape(Squircle(cornerRadius: 11))
		.fieldBox(fill: Palette.sheetCardField, isFocused: focused != nil)
		.onChange(of: draft.proxyHost) { old, new in
			if new.count - old.count > 1 {
				draft.pasteProxy(new)
			}
		}
		.onChange(of: focused) { old, _ in
			if old == .host {
				draft.pasteProxy(draft.proxyHost)
			}
		}
	}

	private var divider: some View {
		Rectangle()
			.fill(Palette.border)
			.frame(width: 1)
	}
}

// 24px, rounded 7: a 16px X, muted until hovered.
private struct CloseButton: View {
	let action: () -> Void

	var body: some View {
		Button(action: action) {
			LucideIcon(.x, size: 16)
		}
		.buttonStyle(.icon(size: 24, shape: .squircle(7), hoverTint: Palette.foreground, fill: nil))
		.accessibilityLabel("Close")
	}
}

// 24px swatches, rounded-md, 8px apart; the selected one is ringed with the
// card then its color.
private struct SwatchPicker: View {
	@Binding var selection: ProfileColor

	var body: some View {
		HStack(spacing: 8) {
			ForEach(ProfileColor.allCases, id: \.self) { swatch in
				let isSelected = swatch == selection

				Button { selection = swatch } label: {
					Squircle(cornerRadius: 11)
						.fill(swatch.color)
						.frame(width: 24, height: 24)
						.background {
							// A ring concentric with the swatch: its 11 grown by
							// the 2- and 3-point offsets (DESIGN.md › Shape).
							if isSelected {
								Squircle(cornerRadius: 17)
									.fill(swatch.color)
									.padding(-3)
								Squircle(cornerRadius: 15)
									.fill(Palette.card)
									.padding(-2)
							}
						}
				}
				.buttonStyle(.plain)
				.accessibilityLabel(swatch.rawValue.capitalized)
				.accessibilityAddTraits(isSelected ? .isSelected : [])
			}
		}
		// Room for the ring.
		.padding(.vertical, 3)
	}
}

// The proxy test (Paper's 05.5): nothing yet, or a dot and its words beside the
// button: yellow while it runs, the proxy's IP on emerald, the error on red.
private enum ProxyTestState: Equatable {
	case untested
	case testing
	case connected(String)
	case failed(String)

	var status: (dot: Color, text: String)? {
		switch self {
		case .untested: nil
		case .testing: (Palette.warning, "Testing\u{2026}")
		case let .connected(ip): (Palette.success, "Connected \u{B7} \(ip)")
		case let .failed(error): (Palette.failure, error)
		}
	}
}
