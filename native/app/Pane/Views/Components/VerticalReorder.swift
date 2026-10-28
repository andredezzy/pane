import PaneKit
import SwiftUI

// Reordering a vertical list by dragging, as the Electron app's sortable
// lists did (dnd-kit, its vertical-axis modifier): the drag starts after 5
// points, and moves up and down only, never out of the list. The row stays in
// the list at 40%, moving to where it would drop as the rows around it slide
// aside, while a copy of it rides the pointer. On release the copy settles
// into the row's place, and the order is kept.
//
// A row is a button, and its drag runs beside the button's press
// (`simultaneousGesture`): as the button's `gesture`, the press held every
// move until the release, and the row moved only then. The press still shows
// at once, and a release on the row after a drag is no click (`isDragging`).
@MainActor
@Observable
final class VerticalReorder {
	struct Drag {
		let id: String
		let start: Int
		// The order and the rows' frames as the drag began, in the list's
		// space.
		let ids: [String]
		let frames: [String: CGRect]
		// The pointer's distance below the row's top.
		let grab: CGFloat
		var pointer: CGFloat
		var target: Int
		// Released: the copy moves onto the row, then goes.
		var settleTop: CGFloat?
	}

	let space: String
	// The copy's height: a profile's header, or a tab's row.
	let rowHeight: CGFloat
	let spacing: CGFloat
	private(set) var drag: Drag?
	// The rows' frames and the list's height, in its space, as laid out.
	@ObservationIgnored var frames: [String: CGRect] = [:]
	@ObservationIgnored var height: CGFloat = 0

	init(space: String, rowHeight: CGFloat, spacing: CGFloat) {
		self.space = space
		self.rowHeight = rowHeight
		self.spacing = spacing
	}

	// A row is dragged, or settling after one: a button's click then is the
	// drag's release.
	var isDragging: Bool {
		drag != nil
	}

	// The order to draw: the dragged row at the place it would drop.
	func order(_ ids: [String]) -> [String] {
		guard let drag, ids == drag.ids else {
			return ids
		}

		return Reorder.order(ids, moving: drag.id, to: drag.target)
	}

	// The copy's top in the list: under the pointer, held inside the list.
	var copyTop: CGFloat {
		guard let drag else {
			return 0
		}

		if let top = drag.settleTop {
			return top
		}

		return min(max(drag.pointer - drag.grab, 0), max(height - rowHeight, 0))
	}

	// The drag for the row `id` of the list's `ids`: its moves, then the
	// order it ends on, to `commit` (from, to) when it changed. The ids are
	// read as the drag starts, so a row need not take the whole list as an
	// input to draw itself.
	func gesture(_ id: String, ids: @escaping @MainActor () -> [String], commit: @escaping (Int, Int) -> Void) -> some Gesture {
		DragGesture(minimumDistance: 5, coordinateSpace: .named(space))
			.onChanged { value in
				MainActor.assumeIsolated {
					self.move(id, ids: ids, start: value.startLocation.y, pointer: value.location.y)
				}
			}
			.onEnded { _ in
				MainActor.assumeIsolated {
					self.end(commit: commit)
				}
			}
	}

	private func move(_ id: String, ids: () -> [String], start: CGFloat, pointer: CGFloat) {
		if drag == nil {
			let ids = ids()

			guard let index = ids.firstIndex(of: id), let frame = frames[id] else {
				return
			}

			drag = Drag(id: id, start: index, ids: ids, frames: frames, grab: start - frame.minY, pointer: pointer, target: index)
		}

		guard var drag, drag.settleTop == nil else {
			return
		}

		drag.pointer = pointer
		let target = target(of: drag)

		if target != drag.target {
			drag.target = target
			withAnimation(Motion.slide) { self.drag = drag }
		} else {
			self.drag = drag
		}
	}

	// Where the copy's top falls among the others (Reorder.target in PaneKit,
	// where it is tested).
	private func target(of drag: Drag) -> Int {
		Reorder.target(
			drag.ids,
			heights: drag.frames.mapValues { Double($0.height) },
			firstTop: Double(drag.ids.compactMap { drag.frames[$0]?.minY }.min() ?? 0),
			dragged: drag.id,
			top: Double(min(max(drag.pointer - drag.grab, 0), max(height - rowHeight, 0))),
			spacing: Double(spacing)
		)
	}

	private func end(commit: @escaping (Int, Int) -> Void) {
		guard var drag else {
			return
		}

		drag.settleTop = frames[drag.id]?.minY ?? copyTop
		withAnimation(Motion.slide) { self.drag = drag }

		let from = drag.start
		let to = drag.target

		DispatchQueue.main.asyncAfter(deadline: .now() + 0.22) {
			if from != to {
				commit(from, to)
			}

			withAnimation(Motion.drop) { self.drag = nil }
		}
	}
}

// Each row's frame in the list's space, for the drag's targets.
struct ReorderFramesKey: PreferenceKey {
	static let defaultValue: [String: CGRect] = [:]

	static func reduce(value: inout [String: CGRect], nextValue: () -> [String: CGRect]) {
		value.merge(nextValue()) { $1 }
	}
}

extension EnvironmentValues {
	// The row is the one dragged: it stands in the list at 40%, under the
	// pointer more often than not, with no hover or press of its own (the
	// copy over it has them).
	@Entry var isDraggedRow = false
}

extension View {
	// A row of a reorderable list: at 40% while it is dragged, its frame
	// reported to the list.
	func reorderRow(_ id: String, in reorder: VerticalReorder) -> some View {
		let isDragged = reorder.drag?.id == id

		return opacity(isDragged ? 0.4 : 1)
			.environment(\.isDraggedRow, isDragged)
			.background {
				GeometryReader { geometry in
					Color.clear.preference(key: ReorderFramesKey.self, value: [id: geometry.frame(in: .named(reorder.space))])
				}
			}
	}

	// The list itself: its space and height, the rows' frames, and the copy
	// of the dragged row over it.
	func reorderList(_ reorder: VerticalReorder, @ViewBuilder copy: @escaping (String) -> some View) -> some View {
		coordinateSpace(name: reorder.space)
			.onPreferenceChange(ReorderFramesKey.self) { frames in
				MainActor.assumeIsolated { reorder.frames = frames }
			}
			.background {
				GeometryReader { geometry in
					Color.clear
						.onAppear { reorder.height = geometry.size.height }
						.onChange(of: geometry.size.height) { _, height in reorder.height = height }
				}
			}
			.overlay(alignment: .topLeading) {
				if let drag = reorder.drag {
					// The row itself stays in the list for VoiceOver.
					copy(drag.id)
						.offset(y: reorder.copyTop)
						.allowsHitTesting(false)
						.accessibilityHidden(true)
				}
			}
	}
}
