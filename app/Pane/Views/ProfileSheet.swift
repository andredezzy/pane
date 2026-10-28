import PaneKit
import SwiftUI

// Create or edit a profile, as the Electron app's sheet: a 360px panel inset
// 8px from the window's top, right, and bottom, with the title and a close X,
// the form (name, group, color, fingerprint, proxy), and Cancel / Create.
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
	@State private var proxyShortcut = ""
	@State private var proxyTest = ProxyTestState.untested
	@State private var spin = false

	private var isNew: Bool { profile == nil }

	var body: some View {
		VStack(spacing: 0) {
			header
			hairline

			ScrollView {
				VStack(alignment: .leading, spacing: 16) {
					VStack(alignment: .leading, spacing: 0) {
						LabeledField(title: "Name") {
							PaneTextField(placeholder: "e.g. Work account", text: $draft.name, focusesOnAppear: true, onSubmit: submit)
								.accessibilityIdentifier("pane.profile.name")
						}

						if problem == .nameMissing {
							Text(ProfileDraft.Problem.nameMissing.message)
								.font(.system(size: 12.8, weight: .medium))
								.foregroundStyle(Palette.destructive)
						}
					}

					LabeledField(title: "Group", note: "(optional)") {
						PaneTextField(placeholder: "Enter group name", text: $draft.group, onSubmit: submit)
					}

					VStack(alignment: .leading, spacing: 0) {
						FormLabel(title: "Color")
							.accessibilityHidden(true)
						SwatchPicker(selection: $draft.color)
							.accessibilityElement(children: .contain)
							.accessibilityLabel("Color")
					}

					switchRow("Spoof fingerprint", isOn: $draft.spoofing)

					if draft.spoofing {
						platformPicker
					}

					Hairline()

					switchRow("Enable proxy", isOn: $draft.proxyEnabled)

					if draft.proxyEnabled {
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

	// The Electron app's header and footer rules: white 5%, which only shows
	// on dark.
	private var hairline: some View {
		Hairline(color: .white.opacity(0.05))
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
				.accessibilityHidden(true)
			PresetRow(
				options: [Platform.windows, .macOS, .linux],
				selection: draft.platform,
				// CSS capitalize of the lowercased name, as the Electron app
				// shows it.
				title: { $0.rawValue.lowercased().capitalized },
				select: { draft.platform = $0 }
			)
			.accessibilityElement(children: .contain)
			.accessibilityLabel("Platform")
			.accessibilityIdentifier("pane.profile.platform")

			if draft.platform != ProfileDraft.hostPlatform {
				Text("Cross-OS spoofing can\u{2019}t fool strict WebGL checks \u{2014} use \(ProfileDraft.hostPlatform.rawValue.lowercased()) or turn spoofing off for Cloudflare-protected sites.")
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
				.accessibilityLabel("Paste a proxy")
				.accessibilityIdentifier("pane.profile.proxyShortcut")

			HStack(spacing: 8) {
				PaneMenuButton(options: ProxyType.allCases.map { ($0.rawValue, $0) }, selection: draft.proxyType, label: "Proxy type", size: .regular, fillsWidth: true) {
					draft.proxyType = $0
				}
				.frame(width: 100)
				PaneTextField(placeholder: "Host", text: $draft.proxyHost)
					.accessibilityLabel("Proxy host")
					.accessibilityIdentifier("pane.profile.proxyHost")
				PaneTextField(placeholder: "Port", text: $draft.proxyPort)
					.frame(width: 80)
					.accessibilityLabel("Proxy port")
					.accessibilityIdentifier("pane.profile.proxyPort")
			}

			// As the name's: a profile that asks for a proxy never saves
			// without one.
			if problem == .portInvalid {
				Text(ProfileDraft.Problem.portInvalid.message)
					.font(.system(size: 12.8, weight: .medium))
					.foregroundStyle(Palette.destructive)
					// Right under the row, as the name's error sits under its field.
					.padding(.top, -12)
					.accessibilityIdentifier("pane.profile.portError")
			}

			HStack(spacing: 8) {
				PaneTextField(placeholder: "Username (optional)", text: $draft.proxyUsername)
					.accessibilityLabel("Proxy username")
				PaneTextField(placeholder: "Password (optional)", text: $draft.proxyPassword, secure: true)
					.accessibilityLabel("Proxy password")
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
			.disabled(proxyTest == .testing || draft.proxyHost.trimmingCharacters(in: .whitespaces).isEmpty)
			.accessibilityIdentifier("pane.profile.testProxy")
		}
		.padding(12)
		.overlay {
			Squircle(cornerRadius: 11)
				.strokeBorder(Palette.border, lineWidth: 1)
		}
	}

	private func pasteProxy(_ raw: String) {
		if draft.pasteProxy(raw) {
			proxyTest = .untested
		}
	}

	private func testProxy() {
		guard let config = draft.proxy else {
			proxyTest = .failed(ProfileDraft.Problem.portMessage)
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

	private func submit() {
		problem = draft.validate()

		if problem == nil {
			save(draft)
			onDone()
		}
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
							// A ring concentric with the swatch: its 11 grown by
							// the 2- and 3-point offsets (DESIGN.md › Shape).
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
			return Palette.successFill
		}

		return nil
	}

	var border: Color? {
		switch self {
		case .connected: Palette.successFill
		case .failed: Palette.destructive
		default: nil
		}
	}
}
