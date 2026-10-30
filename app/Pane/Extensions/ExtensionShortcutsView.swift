import PaneCore
import PaneKit
import SwiftUI

// The Extensions page's Keyboard shortcuts (Paper: 09.9 to 09.12), Pane's
// chrome://extensions/shortcuts, in the App icon dialog's style: the title,
// the profile's enabled extensions with commands, each command's
// description and its shortcut, and Done. A click on a shortcut records the
// next key, with every shortcut paused; Esc, Tab, or a click beside the
// field stops it. Done, Return, Esc while nothing records, or a click
// outside the dialog closes it (KeyRouting, ADR 0104).
struct ExtensionShortcutsDialog: View {
	let controller: ExtensionsController
	let dialog: ShortcutsDialog
	let profileName: String

	var body: some View {
		VStack(alignment: .leading, spacing: 20) {
			VStack(alignment: .leading, spacing: 6) {
				Text("Keyboard shortcuts")
					.font(.system(size: 18, weight: .semibold))
					.tracking(-0.18)
					.foregroundStyle(Palette.foreground)
					.lineBox(24)

				Text("\(profileName)’s extensions. Click a shortcut, then press the keys.")
					.font(.system(size: 14))
					// text-sm: 20px lines, half the leading above the first
					// and below the last.
					.lineSpacing(3)
					.padding(.vertical, 1.5)
					.foregroundStyle(Palette.foreground.opacity(0.7))
					.fixedSize(horizontal: false, vertical: true)
			}

			// A long list scrolls inside the dialog.
			ViewThatFits(in: .vertical) {
				groups

				ScrollView {
					groups
				}
			}

			HStack {
				Spacer()

				Button(action: controller.closeShortcuts) {
					KeyedLabel(title: "Done", key: "↩", role: .primary)
				}
				.buttonStyle(.keyed(.primary))
				.accessibilityIdentifier("pane.extensionShortcuts.done")
			}
			.padding(.top, 8)
		}
		.padding(24)
		.frame(width: 560, alignment: .leading)
		// A click beside the fields stops a recording, as a click outside
		// Chrome's field does.
		.contentShape(Rectangle())
		.onTapGesture(perform: controller.cancelRecording)
		.surfaceBackground(cornerRadius: 29, shadow: Palette.dialogShadow, blur: 64, y: 24)
		// Clear of the window's edges, however long the list.
		.padding(24)
		.accessibilityElement(children: .contain)
		.accessibilityAddTraits(.isModal)
		.accessibilityIdentifier("pane.extensionShortcuts")
	}

	// Each extension, 16 apart: its icon and name on a 28-point line, then
	// its commands.
	private var groups: some View {
		VStack(alignment: .leading, spacing: 16) {
			ForEach(dialog.extensions) { entry in
				VStack(alignment: .leading, spacing: 0) {
					HStack(spacing: 10) {
						Group {
							if let icon = entry.item.icon {
								Image(nsImage: icon)
									.resizable()
									.interpolation(.high)
							} else {
								Text(entry.item.name.prefix(1).uppercased())
									.font(.system(size: 11, weight: .medium))
									.foregroundStyle(.white)
									.frame(width: 20, height: 20)
									.background(Palette.letterTile, in: Squircle(cornerRadius: 7))
							}
						}
						.frame(width: 20, height: 20)
						.accessibilityHidden(true)

						Text(entry.item.name)
							.font(.system(size: 13, weight: .medium))
							.foregroundStyle(Palette.accentForeground)
							.lineLimit(1)
							.lineBox(18)
					}
					.frame(height: 28)

					ForEach(entry.commands, id: \.name) { command in
						row(CommandField(extensionID: entry.item.id, command: command.name), command)
					}
				}
			}
		}
		.frame(maxWidth: .infinity, alignment: .leading)
	}

	// The description beside the field, under the extension's name (30 in);
	// a message under the field, the description level with the field.
	private func row(_ field: CommandField, _ command: ExtensionCommand) -> some View {
		let recording = dialog.recording == field
		let refusal = recording ? dialog.refusal : nil
		let moved = dialog.moved?.field == field ? dialog.moved?.from : nil

		return HStack(alignment: .top, spacing: 16) {
			Text(command.summary)
				.font(.system(size: 13))
				.foregroundStyle(Palette.foreground)
				.lineLimit(1)
				.frame(maxWidth: .infinity, minHeight: 32, alignment: .leading)

			VStack(alignment: .leading, spacing: 6) {
				ShortcutField(
					shortcut: command.shortcut,
					recording: recording,
					held: recording ? dialog.held : [],
					refused: refusal != nil,
					record: { controller.record(field) },
					clear: { controller.clearShortcut(field) }
				)
				.accessibilityLabel("\(command.summary) shortcut")
				.accessibilityIdentifier("pane.extensionShortcuts.\(field.extensionID).\(field.command)")

				if let refusal {
					message(refusal.message, color: Palette.destructive, weight: .medium)
				} else if let moved {
					message("Moved from \(moved)", color: Palette.mutedForeground, weight: .regular)
				}
			}
			.frame(width: 168, alignment: .leading)
		}
		.padding(.leading, 30)
		.padding(.vertical, 4)
	}

	// At 12 on 16-point lines.
	private func message(_ text: String, color: Color, weight: Font.Weight) -> some View {
		Text(text)
			.font(.system(size: 12, weight: weight))
			.foregroundStyle(color)
			.lineSpacing(2)
			.padding(.vertical, 1)
			.fixedSize(horizontal: false, vertical: true)
	}
}

// A command's shortcut (Paper: 09.12): 168 by 32, rounded 11, a 1-point
// border, the text 12 in. The chord at 13 medium, as the menu bar writes it,
// or Not set, muted. Hover lays the foreground at 4%, with Clear when it is
// set; a press, 6%, then recording: the ring outside the border, Type a
// shortcut or the modifiers held with a caret, and the esc hint. A refused
// key turns the ring the destructive color.
private struct ShortcutField: View {
	let shortcut: String
	let recording: Bool
	let held: PaneKit.KeyPress.Modifiers
	let refused: Bool
	let record: () -> Void
	let clear: () -> Void

	@State private var isHovered = false

	var body: some View {
		Button(action: record) {
			HStack(spacing: 0) {
				label

				Spacer(minLength: 0)

				if recording {
					Text("esc")
						.font(.system(size: 11, weight: .medium))
						.foregroundStyle(Palette.mutedForeground)
						.lineBox(14)
						.padding(.horizontal, 5)
						.frame(height: 18)
						.background(Palette.press, in: Squircle(cornerRadius: 7))
				}
			}
		}
		.buttonStyle(ShortcutFieldStyle(isHovered: isHovered && !recording))
		// Clear sits over the field, where its 20-point circle ends 6 in.
		.overlay(alignment: .trailing) {
			if isHovered && !recording && !shortcut.isEmpty {
				Button(action: clear) {
					LucideIcon(.x, size: 12, strokeWidth: 2.25)
				}
				.buttonStyle(.icon(size: 20, tint: Palette.mutedForeground, hoverTint: Palette.accentForeground, fill: nil))
				.padding(.trailing, 7)
				.help("Clear")
				.accessibilityLabel("Clear")
			}
		}
		.overlay {
			if recording {
				Squircle(cornerRadius: 12)
					.strokeBorder(refused ? Palette.destructive : Palette.ring, lineWidth: 1)
					.padding(-1)
			}
		}
		.onHover { hovering in
			withAnimation(Motion.hover) { isHovered = hovering }
		}
		.accessibilityValue(shortcut.isEmpty ? "Not set" : ExtensionShortcut.glyphs(shortcut))
	}

	@ViewBuilder
	private var label: some View {
		if recording && !held.isEmpty {
			HStack(spacing: 2) {
				chord(ExtensionShortcut.glyphs(held))

				Rectangle()
					.fill(Palette.foreground.opacity(0.6))
					.frame(width: 1, height: 16)
			}
		} else if recording {
			plain("Type a shortcut")
		} else if shortcut.isEmpty {
			plain("Not set")
		} else {
			chord(ExtensionShortcut.glyphs(shortcut))
		}
	}

	// 13 medium, tracked 0.04 em.
	private func chord(_ text: String) -> some View {
		Text(text)
			.font(.system(size: 13, weight: .medium))
			.tracking(0.52)
			.foregroundStyle(Palette.foreground)
			.lineBox(18)
	}

	private func plain(_ text: String) -> some View {
		Text(text)
			.font(.system(size: 13))
			.foregroundStyle(Palette.mutedForeground)
			.lineBox(18)
	}
}

// The field's box: its fill under the pointer (4%, fading in 0.15 s) and
// pressed (6%, at once, fading as it lets go), and its border.
private struct ShortcutFieldStyle: ButtonStyle {
	let isHovered: Bool

	func makeBody(configuration: Configuration) -> some View {
		let shape = Squircle(cornerRadius: 11)

		configuration.label
			// 12 and 6 inside the 1-point border.
			.padding(.leading, 13)
			.padding(.trailing, 7)
			.frame(width: 168, height: 32)
			.background {
				ZStack {
					shape.fill(Palette.outlineHover).opacity(isHovered && !configuration.isPressed ? 1 : 0)
					PressLayer(shape: shape, color: Palette.press, isPressed: configuration.isPressed)
				}
			}
			.overlay {
				shape.strokeBorder(Palette.border, lineWidth: 1)
			}
			.contentShape(shape)
	}
}
