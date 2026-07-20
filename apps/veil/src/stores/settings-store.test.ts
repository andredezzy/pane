import { beforeEach, describe, expect, it, vi } from "vitest";

import { settingsStore } from "./settings-store";

// In-memory stand-in for the fs-backed storage so rehydration can be driven
// with hand-written settings.json payloads.
const files = vi.hoisted(() => new Map<string, string>());

vi.mock("./middlewares/fs-storage", () => ({
	fsStorage: {
		getItem: (name: string) => files.get(name) ?? null,
		setItem: (name: string, value: string) => {
			files.set(name, value);
		},
		removeItem: (name: string) => {
			files.delete(name);
		},
	},
}));

describe("settings persistence", () => {
	beforeEach(() => {
		files.clear();
	});

	it("fills defaults for fields a persisted settings.json predates", async () => {
		files.set(
			"settings",
			JSON.stringify({
				state: { settings: { chromiumPath: "/Applications/Chromium.app" } },
				version: 0,
			}),
		);

		await settingsStore.persist.rehydrate();

		expect(settingsStore.getState().settings).toEqual({
			chromiumPath: "/Applications/Chromium.app",
			theme: "system",
		});
	});

	it("restores a persisted theme", async () => {
		files.set(
			"settings",
			JSON.stringify({
				state: { settings: { chromiumPath: "", theme: "dark" } },
				version: 0,
			}),
		);

		await settingsStore.persist.rehydrate();

		expect(settingsStore.getState().settings.theme).toBe("dark");
	});
});
