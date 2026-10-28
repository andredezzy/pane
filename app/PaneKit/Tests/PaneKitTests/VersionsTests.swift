// Ported from apps/veil/src/main/compare-versions.test.ts.
import PaneKit
import Testing

@Suite("isNewerVersion")
struct VersionsTests {
	@Test("is true when the candidate's patch is newer")
	func newerPatch() throws {
		#expect(try isNewerVersion("0.1.2", than: "0.1.1"))
	}

	@Test("is true when the candidate's minor or major is newer")
	func newerMinorOrMajor() throws {
		#expect(try isNewerVersion("0.2.0", than: "0.1.9"))
		#expect(try isNewerVersion("1.0.0", than: "0.9.9"))
	}

	@Test("is false when the versions are equal")
	func equal() throws {
		#expect(try !isNewerVersion("0.1.1", than: "0.1.1"))
	}

	@Test("is false when the candidate is older")
	func older() throws {
		#expect(try !isNewerVersion("0.1.0", than: "0.1.1"))
	}

	@Test("strips a leading v from GitHub-style release tags")
	func leadingV() throws {
		#expect(try isNewerVersion("v0.1.2", than: "0.1.1"))
		#expect(try !isNewerVersion("v0.1.1", than: "v0.1.1"))
	}

	@Test("treats missing trailing segments as zero")
	func missingSegments() throws {
		#expect(try isNewerVersion("0.2", than: "0.1.9"))
		#expect(try !isNewerVersion("1", than: "1.0.0"))
	}

	@Test("throws on a malformed version segment")
	func malformed() {
		#expect(throws: InvalidVersion.self) {
			try isNewerVersion("0.x.1", than: "0.1.0")
		}
	}
}
