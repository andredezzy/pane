import { vi } from "vitest";

vi.stubGlobal("Bun", {
	env: process.env,
	version: "1.0.0-mock",
	sleep: (ms: number) =>
		new Promise((resolve) => globalThis.setTimeout(resolve, ms)),
});
