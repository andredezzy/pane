// The general pasteboard: every item saved to a file and put back, or its
// text. The context menu's copy commands write the user's own clipboard,
// which a scenario leaves as it found it.
// Usage: pasteboard save <file> | restore <file> | text | clear
import AppKit

let pasteboard = NSPasteboard.general
let arguments = CommandLine.arguments

switch arguments.count > 1 ? arguments[1] : "" {
case "save" where arguments.count == 3:
	let items: [[String: String]] = (pasteboard.pasteboardItems ?? []).map { item in
		var entries: [String: String] = [:]

		for type in item.types {
			entries[type.rawValue] = item.data(forType: type)?.base64EncodedString()
		}

		return entries
	}

	let data = try JSONSerialization.data(withJSONObject: items)
	try data.write(to: URL(fileURLWithPath: arguments[2]), options: .atomic)

case "restore" where arguments.count == 3:
	let data = try Data(contentsOf: URL(fileURLWithPath: arguments[2]))
	let items = try JSONSerialization.jsonObject(with: data) as? [[String: String]] ?? []
	pasteboard.clearContents()

	pasteboard.writeObjects(items.map { entries in
		let item = NSPasteboardItem()

		for (type, base64) in entries {
			if let data = Data(base64Encoded: base64) {
				item.setData(data, forType: NSPasteboard.PasteboardType(type))
			}
		}

		return item
	})

case "text":
	print(pasteboard.string(forType: .string) ?? "", terminator: "")

case "clear":
	pasteboard.clearContents()

default:
	FileHandle.standardError.write(Data("usage: pasteboard save <file> | restore <file> | text | clear\n".utf8))
	exit(2)
}
