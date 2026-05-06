import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { app } from "electron";

import { profileStore } from "../../stores/profile-store";

function overwriteAndDelete(filePath: string, passes = 3): void {
	try {
		const stat = fs.statSync(filePath);
		const size = stat.size;

		const fd = fs.openSync(filePath, "w");

		for (let i = 0; i < passes; i++) {
			const randomData = crypto.randomBytes(size);
			fs.writeSync(fd, randomData, 0, size, 0);
			fs.fsyncSync(fd);
		}

		fs.closeSync(fd);
		fs.unlinkSync(filePath);
	} catch {}
}

function overwriteDirectory(dirPath: string, passes = 3): void {
	try {
		const entries = fs.readdirSync(dirPath, { withFileTypes: true });

		for (const entry of entries) {
			const fullPath = path.join(dirPath, entry.name);

			if (entry.isDirectory()) {
				overwriteDirectory(fullPath, passes);
				fs.rmSync(fullPath, { recursive: true, force: true });
			} else {
				overwriteAndDelete(fullPath, passes);
			}
		}
	} catch {}
}

export function executeWipe(): void {
	const userData = app.getPath("userData");
	const tempDir = app.getPath("temp");

	const profiles = profileStore.getState().profiles;

	for (const profile of profiles) {
		const partitionPath = path.join(
			userData,
			"Partitions",
			`persist_profile-${profile.id}`,
		);
		overwriteDirectory(partitionPath);

		try {
			fs.rmSync(partitionPath, { recursive: true, force: true });
		} catch {}
	}

	overwriteAndDelete(path.join(userData, "profiles.json"));
	overwriteAndDelete(path.join(userData, "security.json"));
	overwriteAndDelete(path.join(userData, "settings.json"));

	const fingerprintDirectory = path.join(tempDir, "pane-fingerprints");

	try {
		const fingerprintFiles = fs.readdirSync(fingerprintDirectory);

		for (const file of fingerprintFiles) {
			if (file.startsWith("fp-") && file.endsWith(".js")) {
				overwriteAndDelete(path.join(fingerprintDirectory, file));
			}
		}
	} catch {}

	app.relaunch();
	app.exit(0);
}
