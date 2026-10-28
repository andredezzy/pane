import Foundation

// Where a row dragged up or down a list drops, as dnd-kit's closest center
// picks for the Electron app's sortable lists: the place whose top is nearest
// the dragged copy's, each place's top counted with the dragged row out of
// the list and back in at it, so rows of any height count (an expanded
// profile is its header and its tabs).
public enum Reorder {
	// `ids` in their order, each row's height, the list's first top, the
	// dragged row, the copy's top, and the space between rows.
	public static func target(
		_ ids: [String],
		heights: [String: Double],
		firstTop: Double,
		dragged: String,
		top: Double,
		spacing: Double
	) -> Int {
		var place = firstTop
		var best = 0
		var bestDistance = abs(place - top)
		var index = 0

		for id in ids where id != dragged {
			guard let height = heights[id] else {
				continue
			}

			place += height + spacing
			index += 1

			if abs(place - top) < bestDistance {
				best = index
				bestDistance = abs(place - top)
			}
		}

		return best
	}

	// `ids` with `id` moved to `target`, among the others.
	public static func order(_ ids: [String], moving id: String, to target: Int) -> [String] {
		guard let start = ids.firstIndex(of: id) else {
			return ids
		}

		var order = ids
		order.remove(at: start)
		order.insert(id, at: min(max(target, 0), order.count))

		return order
	}
}
