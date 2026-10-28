import SwiftUI

// Select (packages/ui select.tsx): an input-like trigger, h-9 rounded-md
// with a 1px border, the value in text-sm and a chevron at 50%; its menu is
// the system's.
struct PaneSelect<Option: Hashable>: View {
	let options: [Option]
	@Binding var selection: Option
	let title: (Option) -> String

	var body: some View {
		Menu {
			ForEach(options, id: \.self) { option in
				Button(title(option)) { selection = option }
			}
		} label: {
			HStack(spacing: 8) {
				Text(title(selection))
					.font(.system(size: 14))
					.foregroundStyle(Palette.foreground)
				Spacer(minLength: 0)
				LucideIcon(.chevronDown, size: 16)
					.foregroundStyle(Palette.foreground.opacity(0.5))
			}
			.padding(.horizontal, 12)
			.frame(height: 36)
			.contentShape(Rectangle())
		}
		.menuStyle(.button)
		.buttonStyle(.plain)
		.menuIndicator(.hidden)
		.overlay {
			Squircle(cornerRadius: 11)
				.strokeBorder(Palette.border, lineWidth: 1)
		}
	}
}
