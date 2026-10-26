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
		// OpenBSD's bcrypt, vendored unmodified; these defines stand in for
		// OpenBSD's libc internals (native/third_party/openbsd-bcrypt).
		.target(
			name: "CBcrypt",
			cSettings: [
				.headerSearchPath("."),
				.define("DEF_WEAK(x)", to: ""),
				.define("WRAP(x)", to: "x"),
				.define("explicit_bzero(p,n)", to: "memset_s(p,n,0,n)"),
				.define("__STDC_WANT_LIB_EXT1__", to: "1"),
			]
		),
		.target(name: "PaneKit", dependencies: ["CBcrypt"]),
		.testTarget(name: "PaneKitTests", dependencies: ["PaneKit"]),
	]
)
