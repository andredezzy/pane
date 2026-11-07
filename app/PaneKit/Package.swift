// swift-tools-version: 6.0

import PackageDescription

// Pane's domain logic: models, persistence, and the pure rules the UI runs on.
// No AppKit, no engine — everything here is testable with `swift test`.
let package = Package(
	name: "PaneKit",
	platforms: [.macOS(.v14)],
	products: [
		.library(name: "PaneKit", targets: ["PaneKit"]),
	],
	targets: [
		.target(name: "PaneKit"),
		.testTarget(name: "PaneKitTests", dependencies: ["PaneKit"]),
	]
)
