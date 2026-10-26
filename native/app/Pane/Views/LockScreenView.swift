import PaneKit
import SwiftUI

// The PIN screen (apps/veil lock-screen/page.tsx and components/numpad.tsx):
// a panel over the whole window, 8px in, a back arrow when it can be left,
// and in the middle the title, the dots, the numpad, and the hints.
struct LockScreenView: View {
	let lock: AppLock
	let entry: PinEntry

	var body: some View {
		HStack(spacing: 0) {
			// grid-cols-[5rem_1fr_5rem]
			ZStack {
				if entry.dismissable {
					BackButton { lock.dismiss() }
				}
			}
			.frame(width: 80)
			.frame(maxHeight: .infinity)

			VStack(spacing: 28) {
				if let title = entry.title {
					Text(title)
						.font(.system(size: 13, weight: .light))
						.foregroundStyle(Palette.foreground.opacity(0.5))
				}

				PinDots(length: entry.dots, filled: entry.entered.count, shakes: lock.shakes)

				Numpad(digit: lock.digit, backspace: lock.backspace)

				hints
			}
			.frame(maxWidth: .infinity, maxHeight: .infinity)

			Color.clear.frame(width: 80)
		}
		.background(Palette.card)
		.clipShape(RoundedRectangle(cornerRadius: 10, style: .circular))
		.surfaceBackground(cornerRadius: 10, ring: Palette.border, shadow: PanelShadow.color)
		.padding(8)
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.lockScreen")
	}

	@ViewBuilder
	private var hints: some View {
		if entry.step == .enter {
			if entry.canContinue {
				Button("Continue with \(entry.entered.count)-digit PIN") { lock.continueWithPin() }
					.buttonStyle(HintButtonStyle())
					.accessibilityIdentifier("pane.lockScreen.continue")
			} else {
				Text("Enter at least 4 digits")
					.font(.system(size: 13, weight: .light))
					.foregroundStyle(Palette.foreground.opacity(0.3))
			}
		}

		if let error = entry.error {
			Text(error)
				.font(.system(size: 13, weight: .light))
				.foregroundStyle(Palette.destructive)
		}

		if entry.mode == .unlock, lock.state.failedAttempts > 0 {
			let remaining = lock.state.remainingAttempts

			Text(remaining == 1 ? "Last attempt. All data will be erased." : "\(remaining) attempts remaining")
				.font(.system(size: 13, weight: .light))
				.foregroundStyle(remaining == 1 ? Palette.destructive : Palette.foreground.opacity(0.3))
				.accessibilityIdentifier("pane.lockScreen.attempts")
		}
	}
}

// 13px dots 14px apart: filled for each digit entered, outlined after.
private struct PinDots: View {
	let length: Int
	let filled: Int
	let shakes: Int

	var body: some View {
		HStack(spacing: 14) {
			ForEach(0 ..< length, id: \.self) { index in
				Circle()
					.fill(index < filled ? Palette.foreground : .clear)
					.overlay {
						if index >= filled {
							Circle().strokeBorder(Palette.foreground.opacity(0.25), lineWidth: 1.5)
						}
					}
					.frame(width: 13, height: 13)
					.animation(.easeInOut(duration: 0.15), value: filled)
			}
		}
		// globals.css @keyframes shake: 0.4s, ±8 then ±6.
		.keyframeAnimator(initialValue: 0.0, trigger: shakes) { content, offset in
			content.offset(x: offset)
		} keyframes: { _ in
			KeyframeTrack {
				LinearKeyframe(-8, duration: 0.08, timingCurve: .easeInOut)
				LinearKeyframe(8, duration: 0.08, timingCurve: .easeInOut)
				LinearKeyframe(-6, duration: 0.08, timingCurve: .easeInOut)
				LinearKeyframe(6, duration: 0.08, timingCurve: .easeInOut)
				LinearKeyframe(0, duration: 0.08, timingCurve: .easeInOut)
			}
		}
		.accessibilityElement()
		.accessibilityLabel("\(filled) of \(length) digits")
		.accessibilityIdentifier("pane.lockScreen.dots")
	}
}

// 1–9, then 0 and Delete: 64px circles 14px apart.
private struct Numpad: View {
	let digit: (Character) -> Void
	let backspace: () -> Void

	private static let rows: [[Character?]] = [["1", "2", "3"], ["4", "5", "6"], ["7", "8", "9"], [nil, "0", "⌫"]]

	var body: some View {
		VStack(spacing: 14) {
			ForEach(0 ..< Self.rows.count, id: \.self) { row in
				HStack(spacing: 14) {
					ForEach(0 ..< 3, id: \.self) { column in
						switch Self.rows[row][column] {
						case nil:
							Color.clear.frame(width: 64, height: 64)
						case "⌫":
							Button(action: backspace) {
								BackspaceIcon().frame(width: 22, height: 18)
							}
							.buttonStyle(BackspaceButtonStyle())
							.accessibilityLabel("Delete")
							.accessibilityIdentifier("pane.lockScreen.backspace")
						case let key?:
							Button(String(key)) { digit(key) }
								.buttonStyle(DigitButtonStyle())
								.accessibilityIdentifier("pane.lockScreen.digit.\(key)")
						}
					}
				}
			}
		}
	}
}

// numpad.tsx's digit: a 1px ring at 12%, 26px extralight text at 85%; the
// ring and text darken on hover, and it fills at 10% while pressed.
private struct DigitButtonStyle: ButtonStyle {
	@State private var isHovered = false

	func makeBody(configuration: Configuration) -> some View {
		configuration.label
			.font(.system(size: 26, weight: .thin))
			.foregroundStyle(Palette.foreground.opacity(isHovered || configuration.isPressed ? 1 : 0.85))
			.frame(width: 64, height: 64)
			.background(Circle().fill(Palette.foreground.opacity(configuration.isPressed ? 0.1 : 0)))
			.overlay(Circle().strokeBorder(Palette.foreground.opacity(isHovered || configuration.isPressed ? 0.2 : 0.12), lineWidth: 1))
			.contentShape(Circle())
			.onHover { isHovered = $0 }
	}
}

private struct BackspaceButtonStyle: ButtonStyle {
	@State private var isHovered = false

	func makeBody(configuration: Configuration) -> some View {
		configuration.label
			.foregroundStyle(Palette.foreground.opacity(configuration.isPressed ? 0.8 : isHovered ? 0.6 : 0.35))
			.frame(width: 64, height: 64)
			.contentShape(Circle())
			.onHover { isHovered = $0 }
	}
}

// numpad.tsx's backspace glyph (24×20, stroke 1.2).
private struct BackspaceIcon: View {
	var body: some View {
		Canvas { context, size in
			let scale = CGAffineTransform(scaleX: size.width / 24, y: size.height / 20)
			var path = Path()
			path.move(to: CGPoint(x: 9, y: 2))
			path.addLine(to: CGPoint(x: 20, y: 2))
			path.addArc(tangent1End: CGPoint(x: 22, y: 2), tangent2End: CGPoint(x: 22, y: 4), radius: 2)
			path.addLine(to: CGPoint(x: 22, y: 16))
			path.addArc(tangent1End: CGPoint(x: 22, y: 18), tangent2End: CGPoint(x: 20, y: 18), radius: 2)
			path.addLine(to: CGPoint(x: 9, y: 18))
			path.addLine(to: CGPoint(x: 2, y: 10))
			path.closeSubpath()
			path.move(to: CGPoint(x: 16, y: 7))
			path.addLine(to: CGPoint(x: 12, y: 13))
			path.move(to: CGPoint(x: 12, y: 7))
			path.addLine(to: CGPoint(x: 16, y: 13))

			context.stroke(
				path.applying(scale),
				with: .foreground,
				style: StrokeStyle(lineWidth: 1.2, lineCap: .round, lineJoin: .round)
			)
		}
		.accessibilityHidden(true)
	}
}

// The arrow back out of Set up, Change, or Remove PIN: 30%, 60% on hover.
private struct BackButton: View {
	let action: () -> Void
	@State private var isHovered = false

	var body: some View {
		Button(action: action) {
			LucideIcon(.arrowLeft, size: 20)
		}
		.buttonStyle(.plain)
		.foregroundStyle(Palette.foreground.opacity(isHovered ? 0.6 : 0.3))
		.onHover { isHovered = $0 }
		.accessibilityLabel("Back")
		.accessibilityIdentifier("pane.lockScreen.back")
	}
}

// "Continue with n-digit PIN": 13px light text at 60%, 80% on hover.
private struct HintButtonStyle: ButtonStyle {
	@State private var isHovered = false

	func makeBody(configuration: Configuration) -> some View {
		configuration.label
			.font(.system(size: 13, weight: .light))
			.foregroundStyle(Palette.foreground.opacity(isHovered || configuration.isPressed ? 0.8 : 0.6))
			.onHover { isHovered = $0 }
	}
}
