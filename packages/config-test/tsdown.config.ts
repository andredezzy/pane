import { defineConfig } from "tsdown";

export default defineConfig({
	entry: ["src/**/*.ts", "!src/**/*.test.ts"],
	format: ["esm", "cjs"],
	clean: true,
	treeshake: true,
	dts: {
		sourcemap: true,
	},
	external: ["vitest", "vitest/config"],
});
