import type { UserConfig } from "tsdown";

import { baseConfig, TEST_EXCLUSIONS } from "./base";
import { mergeConfig } from "./merge";

const libraryDefaults: UserConfig = {
	entry: ["src/**/*.ts", ...TEST_EXCLUSIONS],
	format: ["esm", "cjs"],
	minify: true,
};

export function library(overrides: UserConfig = {}): UserConfig {
	return mergeConfig(baseConfig, libraryDefaults, overrides);
}
