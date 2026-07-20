import { describe, expect, it } from "vitest";

import { isNewerVersion } from "./compare-versions";

describe("isNewerVersion", () => {
	it("is true when the candidate's patch is newer", () => {
		expect(isNewerVersion("0.1.2", "0.1.1")).toBe(true);
	});

	it("is true when the candidate's minor or major is newer", () => {
		expect(isNewerVersion("0.2.0", "0.1.9")).toBe(true);
		expect(isNewerVersion("1.0.0", "0.9.9")).toBe(true);
	});

	it("is false when the versions are equal", () => {
		expect(isNewerVersion("0.1.1", "0.1.1")).toBe(false);
	});

	it("is false when the candidate is older", () => {
		expect(isNewerVersion("0.1.0", "0.1.1")).toBe(false);
	});

	it("strips a leading v from GitHub-style release tags", () => {
		expect(isNewerVersion("v0.1.2", "0.1.1")).toBe(true);
		expect(isNewerVersion("v0.1.1", "v0.1.1")).toBe(false);
	});

	it("treats missing trailing segments as zero", () => {
		expect(isNewerVersion("0.2", "0.1.9")).toBe(true);
		expect(isNewerVersion("1", "1.0.0")).toBe(false);
	});

	it("throws on a malformed version segment", () => {
		expect(() => isNewerVersion("0.x.1", "0.1.0")).toThrow();
	});
});
