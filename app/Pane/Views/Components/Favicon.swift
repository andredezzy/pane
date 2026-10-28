import AppKit
import ImageIO
import PaneKit
import SwiftUI

// A tab's favicon from the data: URL tabs store (16 points, as Aside's), or
// the placeholder (a globe in the sidebar) while it has none.
struct FaviconView<Placeholder: View>: View {
	let tabID: String
	let dataURL: String
	@ViewBuilder let placeholder: Placeholder

	// A decode this view waited for, until the cache has it.
	@State private var decoded: (dataURL: String, image: NSImage?)?

	var body: some View {
		// While a new favicon decodes, the tab's last one stays.
		let image = dataURL.isEmpty
			? nil
			: Favicons.cached(tabID, dataURL) ?? (decoded?.dataURL == dataURL ? decoded?.image : Favicons.latest(tabID))

		Group {
			if let image {
				Image(nsImage: image)
					.resizable()
					.interpolation(.high)
					.clipShape(Squircle(cornerRadius: 4))
			} else {
				placeholder
			}
		}
		.frame(width: 16, height: 16)
		.task(id: dataURL) {
			guard !dataURL.isEmpty, Favicons.cached(tabID, dataURL) == nil else {
				return
			}

			let image = await Favicons.image(tabID, dataURL)
			decoded = (dataURL, image)
		}
	}
}

extension FaviconView where Placeholder == LucideIcon {
	init(tabID: String, dataURL: String) {
		self.init(tabID: tabID, dataURL: dataURL) {
			LucideIcon(.globe, size: 16)
		}
	}
}

// Favicons decoded once each, off the main thread, at the 32 pixels they
// show at on a Retina screen, and kept by tab while its favicon stays the
// same: a row compares its tab's data URL to the one decoded (the same
// string, so at once), rather than hashing kilobytes of base64 to find it.
// The cache keeps 512 tabs' and lets the system evict under memory pressure,
// where a dictionary by data URL kept every favicon ever seen (a tab whose
// favicon counts unread mail added one each time it changed).
@MainActor
enum Favicons {
	private final class Entry {
		let dataURL: String
		let image: NSImage?

		init(dataURL: String, image: NSImage?) {
			self.dataURL = dataURL
			self.image = image
		}
	}

	private static let cache: NSCache<NSString, Entry> = {
		let cache = NSCache<NSString, Entry>()
		cache.countLimit = 512
		return cache
	}()

	// Decodes under way, so two views of one tab (its row and the
	// switcher's) wait for the same one.
	private static var decoding: [String: (dataURL: String, task: Task<CGImage?, Never>)] = [:]

	// The tab's favicon once decoded: nil while it is not; nil inside when
	// its data does not decode.
	static func cached(_ tabID: String, _ dataURL: String) -> NSImage?? {
		guard let entry = cache.object(forKey: tabID as NSString), entry.dataURL == dataURL else {
			return nil
		}

		return .some(entry.image)
	}

	// The tab's last decoded favicon, whatever its data URL now.
	static func latest(_ tabID: String) -> NSImage? {
		cache.object(forKey: tabID as NSString)?.image
	}

	// Decodes the tabs' favicons ahead of their rows, which show only once
	// their profile expands: at launch, so none waits then.
	static func warm(_ tabs: [PaneKit.Tab]) {
		for tab in tabs where !tab.favicon.isEmpty && cached(tab.id, tab.favicon) == nil {
			Task { _ = await image(tab.id, tab.favicon) }
		}
	}

	static func image(_ tabID: String, _ dataURL: String) async -> NSImage? {
		if let image = cached(tabID, dataURL) {
			return image
		}

		let task: Task<CGImage?, Never>

		if let pending = decoding[tabID], pending.dataURL == dataURL {
			task = pending.task
		} else {
			task = Task.detached(priority: .userInitiated) { decode(dataURL) }
			decoding[tabID] = (dataURL, task)
		}

		let image = await task.value.map { NSImage(cgImage: $0, size: NSSize(width: 16, height: 16)) }

		// A newer favicon may have started decoding meanwhile: only the
		// latest goes in.
		if decoding[tabID]?.dataURL == dataURL {
			decoding[tabID] = nil
			cache.setObject(Entry(dataURL: dataURL, image: image), forKey: tabID as NSString)
		}

		return image
	}

	// "data:image/png;base64,…" to a bitmap of at most 32 pixels a side,
	// decoded now rather than at its first draw.
	private nonisolated static func decode(_ dataURL: String) -> CGImage? {
		guard
			let comma = dataURL.firstIndex(of: ","),
			let data = Data(base64Encoded: String(dataURL[dataURL.index(after: comma)...])),
			let source = CGImageSourceCreateWithData(data as CFData, nil)
		else {
			return nil
		}

		let options: [CFString: Any] = [
			kCGImageSourceCreateThumbnailFromImageAlways: true,
			kCGImageSourceCreateThumbnailWithTransform: true,
			kCGImageSourceShouldCacheImmediately: true,
			kCGImageSourceThumbnailMaxPixelSize: 32,
		]

		return CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary)
	}
}
