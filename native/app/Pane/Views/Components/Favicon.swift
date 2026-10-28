import AppKit
import SwiftUI

// A favicon from the data: URL tabs store (16 points, as Aside's), or a globe.
struct FaviconView: View {
	let dataURL: String

	var body: some View {
		if let image = Self.image(from: dataURL) {
			Image(nsImage: image)
				.resizable()
				.interpolation(.high)
				.frame(width: 16, height: 16)
				.clipShape(Squircle(cornerRadius: 4))
		} else {
			LucideIcon(.globe, size: 16)
		}
	}

	private static var cache: [String: NSImage] = [:]

	static func image(from dataURL: String) -> NSImage? {
		guard !dataURL.isEmpty else {
			return nil
		}

		if let image = cache[dataURL] {
			return image
		}

		guard
			let comma = dataURL.firstIndex(of: ","),
			let data = Data(base64Encoded: String(dataURL[dataURL.index(after: comma)...])),
			let image = NSImage(data: data)
		else {
			return nil
		}

		cache[dataURL] = image

		return image
	}
}
