import SwiftUI

// The page a tab shows while a real Chrome signs in to Google (apps/veil
// profile/google/auth-page.ts), drawn natively with that page's own dark
// colors and type: what to do in Chrome, then Transfer, which brings the
// session back into the profile.
struct GoogleAuthView: View {
	let state: GoogleSignIn.State
	let transfer: () -> Void

	private static let steps = [
		"Sign in to your Google account in the Chrome window",
		"Once signed in, come back to Pane",
		"Click the button below to transfer your session",
	]

	var body: some View {
		VStack(spacing: 32) {
			Image(nsImage: GoogleLogo.image)
				.resizable()
				.frame(width: 40, height: 40)
				.accessibilityHidden(true)

			Text("Sign in with Google Chrome")
				.font(.system(size: 28, weight: .medium))
				.tracking(-0.84)
				.foregroundStyle(Color(rgb: 0xFAFAFA))

			Text("Google requires signing in through a supported browser. A Chrome window has been opened for you.")
				.font(.system(size: 15))
				.lineSpacing(6)
				.multilineTextAlignment(.center)
				.foregroundStyle(Color(rgb: 0x71717A))
				.frame(maxWidth: 480)

			VStack(alignment: .leading, spacing: 12) {
				ForEach(Array(Self.steps.enumerated()), id: \.offset) { index, step in
					HStack(alignment: .top, spacing: 12) {
						Text("\(index + 1)")
							.font(.system(size: 11))
							.foregroundStyle(Color(rgb: 0x71717A))
							.frame(width: 20, height: 20)
							.background(Circle().fill(Color(rgb: 0x27272A)))
							.padding(.top, 1)

						Text(step)
							.font(.system(size: 14))
							.lineSpacing(4)
							.foregroundStyle(Color(rgb: 0xA1A1AA))
					}
				}
			}
			.frame(maxWidth: 400, alignment: .leading)
			.padding(.vertical, 8)

			Button(state.transferring ? "Transferring session..." : "Transfer session to Pane", action: transfer)
				.buttonStyle(TransferButtonStyle())
				.disabled(state.transferring)
				.padding(.top, 8)
				.accessibilityIdentifier("pane.googleAuth.transfer")

			if let error = state.error {
				Text(error)
					.font(.system(size: 13))
					.multilineTextAlignment(.center)
					.foregroundStyle(Color(rgb: 0xF87171))
					.frame(maxWidth: 400)
					.accessibilityIdentifier("pane.googleAuth.error")
			}
		}
		.padding(.horizontal, 24)
		.padding(.vertical, 40)
		.frame(maxWidth: .infinity, maxHeight: .infinity)
		.background(Color(rgb: 0x0A0A0A))
		.environment(\.colorScheme, .dark)
		.accessibilityElement(children: .contain)
		.accessibilityIdentifier("pane.googleAuth")
	}
}

// The page's button: white on near-black, a step darker on hover, pressed in
// a little.
private struct TransferButtonStyle: ButtonStyle {
	@Environment(\.isEnabled) private var isEnabled
	@State private var isHovered = false

	func makeBody(configuration: Configuration) -> some View {
		configuration.label
			.font(.system(size: 15, weight: .medium))
			.foregroundStyle(Color(rgb: 0x09090B))
			.padding(.horizontal, 40)
			.padding(.vertical, 14)
			.background(
				RoundedRectangle(cornerRadius: 10, style: .circular)
					.fill(Color(rgb: isHovered && isEnabled ? 0xE4E4E7 : 0xFAFAFA))
			)
			.scaleEffect(configuration.isPressed && isEnabled ? 0.98 : 1)
			.opacity(isEnabled ? 1 : 0.4)
			.onHover { isHovered = $0 }
			.contentShape(Rectangle())
	}
}

// Google's "G", the page's own SVG.
private enum GoogleLogo {
	@MainActor static let image = NSImage(data: Data(#"""
	<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48" viewBox="0 0 48 48">
	<path fill="#EA4335" d="M24 9.5c3.54 0 6.71 1.22 9.21 3.6l6.85-6.85C35.9 2.38 30.47 0 24 0 14.62 0 6.51 5.38 2.56 13.22l7.98 6.19C12.43 13.72 17.74 9.5 24 9.5z"/>
	<path fill="#4285F4" d="M46.98 24.55c0-1.57-.15-3.09-.38-4.55H24v9.02h12.94c-.58 2.96-2.26 5.48-4.78 7.18l7.73 6c4.51-4.18 7.09-10.36 7.09-17.65z"/>
	<path fill="#34A853" d="M10.53 28.59A14.5 14.5 0 019.5 24c0-1.59.28-3.14.76-4.59l-7.98-6.19C.92 16.46 0 20.12 0 24c0 3.88.92 7.54 2.56 10.78l7.97-6.19z"/>
	<path fill="#FBBC05" d="M24 48c6.48 0 11.93-2.13 15.89-5.81l-7.73-6c-2.15 1.45-4.92 2.3-8.16 2.3-6.26 0-11.57-4.22-13.47-9.91l-7.98 6.19C6.51 42.62 14.62 48 24 48z"/>
	</svg>
	"""#.utf8)) ?? NSImage()
}
