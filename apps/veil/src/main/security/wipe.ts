import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { app } from "electron";

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

function wipeDirectory(dirPath: string): void {
	overwriteDirectory(dirPath);

	try {
		fs.rmSync(dirPath, { recursive: true, force: true });
	} catch (error) {
		console.warn("[Wipe] Failed to remove directory:", dirPath, error);
	}
}

export function executeWipe(): void {
	const userData = app.getPath("userData");
	const tempDir = app.getPath("temp");

	// Wipe the whole Partitions/ tree in one shot. This covers current profiles, the
	// orphaned partitions of already-deleted profiles (Electron never removes a
	// persist: partition dir, so its cookies/storage outlive the profile and the
	// store entry), pane-internal (the CWS session), and any future partition —
	// without depending on the live profile store.
	wipeDirectory(path.join(userData, "Partitions"));

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

	// An in-flight Google sign-in leaves a pane-google-auth-<random> Chrome user-data
	// dir under tempDir holding live Google cookies. The detached Chrome is untracked
	// here, but removing its data dir destroys the at-rest credentials.
	try {
		for (const entry of fs.readdirSync(tempDir)) {
			if (entry.startsWith("pane-google-auth-")) {
				wipeDirectory(path.join(tempDir, entry));
			}
		}
	} catch (error) {
		console.warn("[Wipe] Failed to scan temp for auth dirs:", error);
	}

	app.relaunch();
	app.exit(0);
}
