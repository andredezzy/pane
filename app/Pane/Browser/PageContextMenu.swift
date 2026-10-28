import AppKit
import PaneCore

// A page's right-click menu, in the Electron app's order
// (packages/electron-chrome-context-menu): link, media, editing or the
// selection, else navigation; then Inspect.
@MainActor
enum PageContextMenu {
	static func build(_ context: ContextMenu, page: WebContents, controller: BrowserController) -> NSMenu {
		let menu = NSMenu()
		menu.autoenablesItems = false

		func add(_ title: String, enabled: Bool = true, _ action: @escaping () -> Void) {
			menu.addItem(ActionMenuItem(title: title, enabled: enabled, action: action))
		}

		func separate() {
			if let last = menu.items.last, !last.isSeparatorItem {
				menu.addItem(.separator())
			}
		}

		if let link = context.linkURL {
			add("Open Link in New Tab") { controller.openInNewTab(link, besides: page) }
			add("Save Link As…") { context.saveLinkAs() }
			add("Copy Link Address") { context.copyLinkAddress() }
			separate()
		}

		switch context.mediaType {
		case .image:
			if let source = context.sourceURL {
				add("Open Image in New Tab") { controller.openInNewTab(source, besides: page) }
				add("Save Image As…") { context.saveSourceAs() }
			}

			add("Copy Image") { context.copyImage() }

			if context.sourceURL != nil {
				add("Copy Image Address") { context.copySourceAddress() }
			}

			separate()

		case .video, .audio:
			let noun = context.mediaType == .video ? "Video" : "Audio"

			if let source = context.sourceURL {
				add("Open \(noun) in New Tab") { controller.openInNewTab(source, besides: page) }
				add("Save \(noun) As…") { context.saveSourceAs() }
				add("Copy \(noun) Address") { context.copySourceAddress() }
				separate()
			}

		default:
			break
		}

		if context.isEditable {
			if !context.misspelledWord.isEmpty {
				for suggestion in context.dictionarySuggestions {
					add(suggestion) { context.replaceMisspelling(with: suggestion) }
				}

				if context.dictionarySuggestions.isEmpty {
					add("No Guesses Found", enabled: false) {}
				}

				separate()
				add("Add to Dictionary") { context.addToDictionary() }
			} else {
				add("Emoji & Symbols") { NSApp.orderFrontCharacterPalette(nil) }
				separate()
				add("Undo", enabled: context.canUndo) { context.undo() }
				add("Redo", enabled: context.canRedo) { context.redo() }
			}

			separate()
			add("Cut", enabled: context.canCut) { context.cut() }
			add("Copy", enabled: context.canCopy) { context.copySelection() }
			add("Paste", enabled: context.canPaste) { context.paste() }
			add("Delete", enabled: context.canDelete) { context.deleteSelection() }
			separate()

			if context.canSelectAll {
				add("Select All") { context.selectAll() }
				separate()
			}
		} else if !context.selectionText.isEmpty {
			add("Copy") { context.copySelection() }

			let selection = context.selectionText.trimmingCharacters(in: .whitespacesAndNewlines)

			if !selection.isEmpty, let search = searchURL(selection) {
				let shown = selection.count > 30 ? "\(selection.prefix(30))…" : selection
				add("Search Google for “\(shown)”") { controller.openInNewTab(search, besides: page) }
			}

			separate()
		}

		if menu.items.isEmpty {
			add("Back", enabled: page.canGoBack) { page.goBack() }
			add("Forward", enabled: page.canGoForward) { page.goForward() }
			add("Reload") { page.reload() }
			separate()
		}

		add("Inspect") { context.inspectElement() }

		return menu
	}

	private static func searchURL(_ text: String) -> URL? {
		var components = URLComponents(string: "https://www.google.com/search")
		components?.queryItems = [URLQueryItem(name: "q", value: text)]

		return components?.url
	}
}

// A menu item that runs a closure.
final class ActionMenuItem: NSMenuItem {
	private let run: () -> Void

	init(title: String, enabled: Bool, action: @escaping () -> Void) {
		run = action
		super.init(title: title, action: #selector(runAction), keyEquivalent: "")
		target = self
		isEnabled = enabled
	}

	@available(*, unavailable)
	required init(coder: NSCoder) {
		fatalError("init(coder:) is not supported")
	}

	@objc private func runAction() {
		run()
	}
}
