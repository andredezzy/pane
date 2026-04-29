import type { UserConfig } from "tsdown";

import { baseConfig, TEST_EXCLUSIONS } from "./base.js";
import { mergeConfig } from "./merge.js";

const reactDefaults: UserConfig = {
	entry: ["src/**/*.ts", "src/**/*.tsx", ...TEST_EXCLUSIONS],
	format: ["esm", "cjs"],
	minify: true,
	external: ["react", "react-dom", "react/jsx-runtime"],
};

export function react(overrides: UserConfig = {}): UserConfig {
	return mergeConfig(baseConfig, reactDefaults, overrides);
}
