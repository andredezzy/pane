import AppKit
import PaneCore
import PaneKit

// The state each reply carries: what the app shows and holds, read as the
// user would see it, plus what the probe reported.
extension Automation {
	// A cookie without its value: where it applies, and its attributes.
	static func describe(_ cookie: Cookie) -> [String: Any] {
		let sameSite = switch cookie.sameSite {
		case .none: "none"
		case .lax: "lax"
		case .strict: "strict"
		default: "unspecified"
		}

		return [
			"name": cookie.name,
			"domain": cookie.domain,
			"path": cookie.path,
			"secure": cookie.isSecure,
			"httpOnly": cookie.isHTTPOnly,
			"sameSite": sameSite,
			"expires": cookie.expirationDate?.timeIntervalSince1970 ?? NSNull(),
		]
	}

	// What a view shows, for telling windows apart: its tooltips, labels,
	// and button titles.
	private static func contents(of view: NSView?) -> [String] {
		guard let view else {
			return []
		}

		var parts: [String] = []

		if let tip = view.toolTip, !tip.isEmpty {
			parts.append("tooltip: \(tip)")
		}

		if let label = view.accessibilityLabel(), !label.isEmpty {
			parts.append("label: \(label)")
		}

		if let button = view as? NSButton, !button.title.isEmpty {
			parts.append("button: \(button.title)")
		}

		return parts + view.subviews.flatMap { contents(of: $0) }
	}

	static func buttons(in view: NSView?) -> [NSButton] {
		guard let view else {
			return []
		}

		return ((view as? NSButton).map { [$0] } ?? []) + view.subviews.flatMap { buttons(in: $0) }
	}

	// Every visible window of the process, by what it is to the app: its
	// window, the sheet on it, an app-modal panel, or other (Chrome's own).
	private func nativeWindows() -> [[String: Any]] {
		let main = controller.browserWindow.window

		return NSApp.windows.filter(\.isVisible).map { window in
			let role = if window === main {
				"main"
			} else if window === main.attachedSheet {
				"sheet"
			} else if window === NSApp.modalWindow {
				"modal"
			} else if contextMenuOpen, String(describing: type(of: window)).contains("Menu") {
				"menu"
			} else if window.identifier?.rawValue == "pane.devtools" {
				"devtools"
			} else if let popover = controller.extensions.popover.window, window === popover {
				"popover"
			} else if ["_NSFullScreenTransition", "NSToolbarFullScreenWindow"].contains(where: String(describing: type(of: window)).hasPrefix) {
				// AppKit's own: the full screen animation, and the title bar
				// it reveals at the top of a full screen window.
				"system"
			} else {
				"other"
			}

			return [
				"role": role,
				"class": String(describing: type(of: window)),
				"title": window.title,
				"frame": NSStringFromRect(window.frame),
				"shows": Self.contents(of: window.contentView),
			]
		}
	}

	// A menu's items, with a check on one that shows it and a submenu's own.
	private static func menuItems(_ menu: NSMenu) -> [[String: Any]] {
		menu.items.filter { !$0.isSeparatorItem }.map { item in
			var entry: [String: Any] = ["title": item.title, "enabled": item.isEnabled, "checked": item.state == .on]

			if let submenu = item.submenu {
				entry["submenu"] = menuItems(submenu)
			}

			return entry
		}
	}

	private func sheetSnapshot() -> Any {
		// An extension popup answers as a sheet did: its buttons, accept first.
		if let prompt = controller.extensions.prompt {
			return [
				"kind": prompt.kind.rawValue,
				"tabID": NSNull(),
				"title": prompt.title,
				"message": ([prompt.permissionsHeading] + prompt.permissions).filter { !$0.isEmpty }.joined(separator: "\n"),
				"buttons": [prompt.acceptTitle, prompt.cancelTitle],
				"fields": [String](),
			] as [String: Any]
		}

		guard let presented = controller.dialogs.presented else {
			return NSNull()
		}

		if let dialog = presented.dialog {
			return [
				"kind": presented.pending.request.kind,
				"tabID": presented.pending.tabID ?? NSNull(),
				"title": dialog.title,
				"message": dialog.message.joined(separator: "\n"),
				"buttons": dialog.buttons,
				"fields": controller.dialogs.fieldTexts,
			] as [String: Any]
		}

		return [
			"kind": presented.pending.request.kind,
			"panel": presented.panel is NSOpenPanel ? "open" : "save",
			"message": presented.panel?.message ?? "",
		] as [String: Any]
	}

	// Whether an image draws anything: an icon can be there and blank.
	private static func hasVisiblePixels(_ image: NSImage) -> Bool {
		guard
			let bitmap = NSBitmapImageRep(
				bitmapDataPlanes: nil, pixelsWide: 16, pixelsHigh: 16, bitsPerSample: 8,
				samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
				bytesPerRow: 0, bitsPerPixel: 0
			),
			let context = NSGraphicsContext(bitmapImageRep: bitmap)
		else {
			return false
		}

		NSGraphicsContext.saveGraphicsState()
		NSGraphicsContext.current = context
		image.draw(in: NSRect(x: 0, y: 0, width: 16, height: 16))
		NSGraphicsContext.restoreGraphicsState()

		return (0..<16).contains { x in
			(0..<16).contains { y in (bitmap.colorAt(x: x, y: y)?.alphaComponent ?? 0) > 0.3 }
		}
	}

	// What has keyboard focus: a text field being edited, by its placeholder
	// (SwiftUI keeps accessibility identifiers off the AppKit field), else
	// the focused view's identifier or class.
	// The address bar's load progress as drawn now: started, the share
	// filled, the opacity, and whether the slow mode's shimmer runs.
	private var progressBar: [String: Any] {
		let bar = controller.progressBar
		let now = CACurrentMediaTime()

		return [
			"started": bar.isStarted,
			"progress": bar.progress,
			"alpha": bar.alpha(at: now),
			"shimmer": bar.shimmer(at: now) != nil,
		]
	}

	private var focus: String {
		let responder = controller.browserWindow.window.firstResponder

		if let editor = responder as? NSTextView, let field = editor.delegate as? NSTextField {
			// SwiftUI gives a prompt as an attributed placeholder.
			return "field: \(field.placeholderString ?? field.placeholderAttributedString?.string ?? "")"
		}

		if let view = responder as? NSView, !view.accessibilityIdentifier().isEmpty {
			return view.accessibilityIdentifier()
		}

		return responder.map { String(describing: type(of: $0)) } ?? "none"
	}

	func snapshot() -> [String: Any] {
		let model = controller.model
		let state = controller.state
		let window = controller.browserWindow.window

		return [
			"profiles": model.profiles.map { profile in
				[
					"id": profile.id,
					"name": profile.name,
					"color": profile.color.rawValue,
					"expanded": model.expandedProfileIDs.contains(profile.id),
					"proxyStatus": model.proxyStatus[profile.id]?.rawValue ?? NSNull(),
					"keepLoaded": profile.keepLoaded,
					"cacheSize": cacheSizes[profile.id].map { $0 as Any } ?? NSNull(),
					"cookies": cookies[profile.id] ?? NSNull(),
					// What the engine applied to the open profile.
					"network": controller.context(for: profile.id).map { context in
						[
							"proxyRules": context.proxyRules ?? NSNull(),
							"webRTCIPHandlingPolicy": context.webRTCIPHandlingPolicy,
						] as [String: Any]
					} ?? NSNull(),
					"tabs": profile.tabs.map { tab in
						[
							"id": tab.id,
							"url": tab.url,
							"title": tab.title,
							"hasFavicon": !tab.favicon.isEmpty,
							"loaded": model.loadedTabIDs.contains(tab.id),
							"loading": model.loadingProgress[tab.id] != nil,
							"canGoBack": model.navigation[tab.id]?.canGoBack ?? false,
							"canGoForward": model.navigation[tab.id]?.canGoForward ?? false,
						] as [String: Any]
					},
				] as [String: Any]
			},
			"update": [
				"status": controller.updater.status.rawValue,
				"available": controller.updater.available?.version ?? NSNull(),
				"progress": controller.updater.downloadProgress ?? NSNull(),
				"eta": controller.updater.downloadETA ?? NSNull(),
				"pill": controller.updater.showsPill,
				// Staged beside the app, to install at quit (ADR 0092).
				"installsItself": controller.updater.staged != nil,
				"checked": controller.updater.lastCheckedAt != nil,
			] as [String: Any],
			"quitConfirmation": controller.state.confirmingQuit,
			"appIconPicker": controller.state.choosingAppIcon,
			"profileSheet": controller.state.profileSheet.map { target in
				target.profileID ?? "new"
			} ?? NSNull(),
			"hostWebGL": controller.hostWebGL.map { ["vendor": $0.vendor, "renderer": $0.renderer] } ?? NSNull(),
			"security": [
				"pinLength": controller.lock.state.pin?.length ?? NSNull(),
				"failedAttempts": controller.lock.state.failedAttempts,
				"locked": controller.lock.isLocked,
				"wipe": ["erasing": AppLock.isErasing, "failures": AppLock.eraseFailures] as [String: Any],
				"screen": controller.lock.entry.map { entry in
					[
						"mode": "\(entry.mode)",
						"step": "\(entry.step)",
						"title": entry.title ?? NSNull(),
						"dots": entry.dots,
						"entered": entry.entered.count,
						"busy": entry.busy,
						"error": entry.error ?? NSNull(),
					] as [String: Any]
				} ?? NSNull(),
			] as [String: Any],
			"activeTabID": model.activeTabID ?? NSNull(),
			"activeProfileID": model.activeProfileID ?? NSNull(),
			"page": String(describing: model.page),
			"theme": model.settings.theme.rawValue,
			"appearance": NSApp.effectiveAppearance.bestMatch(from: [.aqua, .darkAqua])?.rawValue ?? "",
			"appIcon": model.settings.appIcon.rawValue,
			"dockIcon": controller.dockIcon.shown?.rawValue ?? "",
			"find": [
				"visible": state.find.visible,
				"text": state.find.text,
				"matches": state.find.matches,
				"activeMatch": state.find.activeMatch,
			] as [String: Any],
			"switcher": [
				"visible": state.switcher.visible,
				"selectedIndex": state.switcher.selectedIndex,
				"groups": state.switcher.groups.map { group in
					["profileID": group.id, "name": group.name, "tabs": group.tabs.map(\.id)] as [String: Any]
				},
			] as [String: Any],
			"downloads": Array(downloads.values),
			"address": controller.state.address,
			"progressBar": progressBar,
			"window": [
				"title": window.title,
				"key": window.isKeyWindow,
				"visible": window.isVisible,
				"fullScreen": window.styleMask.contains(.fullScreen),
				"frame": NSStringFromRect(window.frame),
				// The close button's origin from the window's top left.
				"trafficLights": window.standardWindowButton(.closeButton).map { button -> [Double] in
					let frame = button.convert(button.bounds, to: nil)
					return [frame.minX, window.frame.height - frame.maxY]
				} ?? [],
				// Each button's frame from the window's top left: x, y, width, height.
				"trafficLightFrames": [NSWindow.ButtonType.closeButton, .miniaturizeButton, .zoomButton].compactMap { type -> [Double]? in
					window.standardWindowButton(type).map { button in
						let frame = button.convert(button.bounds, to: nil)
						return [frame.minX, window.frame.height - frame.maxY, frame.width, frame.height]
					}
				},
			] as [String: Any],
			"closedTabs": model.history.closed.count,
			"pendingProfileDeletions": pendingDeletions.count,
			"sheet": sheetSnapshot(),
			"pendingRequests": controller.dialogs.pendingCount,
			"contextMenu": contextMenu.map(Self.menuItems) ?? NSNull(),
			"contextMenusShown": contextMenusShown,
			"modal": (NSApp.modalWindow).map { modal in
				["title": modal.title, "buttons": Self.buttons(in: modal.contentView).map(\.title)] as [String: Any]
			} ?? NSNull(),
			"nativeWindows": nativeWindows(),
			"extensions": controller.extensions.lists.mapValues { list in
				list.map { item in
					[
						"id": item.id,
						"name": item.name,
						"version": item.version,
						"enabled": item.enabled,
						"pinned": item.pinned,
						"hasAction": item.hasAction,
						"hasPopup": item.hasPopup,
						"actionTitle": item.actionTitle,
						"badgeText": item.badgeText,
						"hasIcon": item.icon != nil,
						"iconVisible": item.icon.map(Self.hasVisiblePixels) ?? false,
					] as [String: Any]
				}
			},
			"extensionPopup": controller.extensions.popover.popup.map { popup in
				[
					"extension": popup.extensionIdentifier,
					"open": popup.isOpen,
					"width": popup.preferredSize.width,
					"height": popup.preferredSize.height,
				] as [String: Any]
			} ?? NSNull(),
			"extensionDetails": controller.extensions.details.map(Self.describe) ?? NSNull(),
			"extensionsMenu": controller.extensions.menuOpen,
			"sidebarWidth": controller.state.sidebarWidth,
			"extensionMoreMenu": controller.extensions.moreMenu?.extensionID ?? NSNull(),
			"pageFullscreen": controller.state.pageFullscreen,
			"exclusiveAccessHint": controller.state.exclusiveAccessHint ?? NSNull(),
			"crashedTabs": Array(model.crashedTabIDs),
			"pictureInPictureTabs": Array(model.pictureInPictureTabIDs),
			"devToolsTabs": Array(model.devToolsTabIDs),
			"printRequests": printRequests,
			"focus": focus,
		]
	}

	static func describe(_ shown: ShownDetails) -> [String: Any] {
		let access: String = switch shown.engine?.siteAccess {
		case .onClick: "onClick"
		case .onSpecificSites: "onSpecificSites"
		case .onAllSites: "onAllSites"
		default: "none"
		}

		return [
			"extension": shown.extensionID,
			"profile": shown.profileID,
			"size": shown.size ?? NSNull(),
			"source": shown.engine?.source ?? NSNull(),
			"permissions": shown.engine?.permissions ?? [],
			"siteAccess": access,
			"wantsFileAccess": shown.engine?.wantsFileAccess ?? false,
			"allowsFileAccess": shown.engine?.allowsFileAccess ?? false,
			"homepage": shown.engine?.homepageURL?.absoluteString ?? NSNull(),
			"webStore": shown.engine?.webStoreURL?.absoluteString ?? NSNull(),
		]
	}

	static func describe(_ download: Download) -> [String: Any] {
		let state = switch download.state {
		case .inProgress: "inProgress"
		case .complete: "complete"
		case .cancelled: "cancelled"
		case .interrupted: "interrupted"
		@unknown default: "unknown"
		}

		return [
			"id": download.identifier,
			"url": download.url.absoluteString,
			"path": download.fileURL?.path ?? NSNull(),
			"receivedBytes": download.receivedBytes,
			"totalBytes": download.totalBytes,
			"state": state,
		]
	}
}
