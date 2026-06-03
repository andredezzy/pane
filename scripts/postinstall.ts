#!/usr/bin/env bun

// Auto-build all workspaces after a local install. Skipped in CI, where the
// build runs explicitly (see .github/workflows/release.yml). Written as a
// script rather than an inline shell guard so it is identical across macOS,
// Linux, and Windows — Bun's shell has no `test` builtin.
if (!process.env.CI) {
	const result = Bun.spawnSync(["bun", "run", "build"], {
		stdio: ["inherit", "inherit", "inherit"],
	});

	process.exit(result.exitCode ?? 0);
}
