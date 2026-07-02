import { describe, expect, it } from "vitest";

import { reconcileChromeVersion } from "./client-hints";

const REAL_UA =
	"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.7680.65 Safari/537.36";

describe("reconcileChromeVersion", () => {
	it("rewrites a stale claimed Chrome version to the real engine's", () => {
		const claimed =
			"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36";

		expect(reconcileChromeVersion(claimed, REAL_UA)).toBe(
			"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.7680.65 Safari/537.36",
		);
	});

	it("leaves the rest of the claimed UA (platform token, Safari suffix) intact", () => {
		const claimed =
			"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36";

		expect(reconcileChromeVersion(claimed, REAL_UA)).toBe(
			"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.7680.65 Safari/537.36",
		);
	});

	it("is a no-op when the claimed version already matches the engine", () => {
		expect(reconcileChromeVersion(REAL_UA, REAL_UA)).toBe(REAL_UA);
	});

	it("returns the claimed UA unchanged when the real UA carries no Chrome token", () => {
		const claimed =
			"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36";

		expect(reconcileChromeVersion(claimed, "Mozilla/5.0 (weird runtime)")).toBe(
			claimed,
		);
	});
});
