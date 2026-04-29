import type { UserConfig } from "tsdown";

import { baseConfig, TEST_EXCLUSIONS } from "./base.js";
import { mergeConfig } from "./merge.js";

const libraryDefaults: UserConfig = {
	entry: ["src/**/*.ts", ...TEST_EXCLUSIONS],
	format: ["esm", "cjs"],
	minify: true,
};

export function library(overrides: UserConfig = {}): UserConfig {
	return mergeConfig(baseConfig, libraryDefaults, overrides);
}
