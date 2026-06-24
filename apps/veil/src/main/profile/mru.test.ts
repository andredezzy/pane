import { describe, expect, it } from "vitest";

import { mostRecentTab } from "./mru";

const tabs = [{ id: "a" }, { id: "b" }, { id: "c" }];

describe("mostRecentTab", () => {
	it("returns the most recently used tab still present", () => {
		// b is more recent than c in the history, and both still exist.
		expect(mostRecentTab(tabs, ["x", "b", "c", "a"])?.id).toBe("b");
	});

	it("skips history ids that are no longer present", () => {
		expect(mostRecentTab(tabs, ["gone", "c", "a"])?.id).toBe("c");
	});

	it("falls back to the last tab when none are in the history", () => {
		expect(mostRecentTab(tabs, [])?.id).toBe("c");
		expect(mostRecentTab(tabs, ["nope"])?.id).toBe("c");
	});

	it("returns undefined when there are no tabs", () => {
		expect(mostRecentTab([], ["a"])).toBeUndefined();
	});
});
