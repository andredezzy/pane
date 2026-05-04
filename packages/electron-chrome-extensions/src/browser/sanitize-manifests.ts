import fs from "node:fs";
import path from "node:path";

const UNSUPPORTED_PERMISSIONS = new Set([
	"bookmarks",
	"contextMenus",
	"downloads",
	"favicon",
	"fontSettings",
	"notifications",
	"privacy",
	"webNavigation",
]);

function sanitizeCSP(csp: string): string {
	return csp
		.split(";")
		.filter((d) => !d.trim().startsWith("navigate-to"))
		.join(";");
}

export function sanitizeExtensionManifests(extensionsPath: string): boolean {
	if (!fs.existsSync(extensionsPath)) {
		return false;
	}
	let anyModified = false;

	for (const extId of fs.readdirSync(extensionsPath)) {
		const extDir = path.join(extensionsPath, extId);

		if (!fs.statSync(extDir).isDirectory()) {
			continue;
		}

		for (const version of fs.readdirSync(extDir)) {
			const manifestPath = path.join(extDir, version, "manifest.json");

			if (!fs.existsSync(manifestPath)) {
				continue;
			}

			try {
				const raw = fs.readFileSync(manifestPath, "utf-8");
				const manifest = JSON.parse(raw);
				let modified = false;

				if (Array.isArray(manifest.permissions)) {
					const filtered = manifest.permissions.filter(
						(p: string) => !UNSUPPORTED_PERMISSIONS.has(p),
					);

					if (filtered.length !== manifest.permissions.length) {
						manifest.permissions = filtered;
						modified = true;
					}
				}

				if (Array.isArray(manifest.optional_permissions)) {
					const filtered = manifest.optional_permissions.filter(
						(p: string) => !UNSUPPORTED_PERMISSIONS.has(p),
					);

					if (filtered.length !== manifest.optional_permissions.length) {
						manifest.optional_permissions = filtered;
						modified = true;
					}
				}

				const csp = manifest.content_security_policy;

				if (typeof csp === "string") {
					const sanitized = sanitizeCSP(csp);

					if (sanitized !== csp) {
						manifest.content_security_policy = sanitized;
						modified = true;
					}
				} else if (csp && typeof csp === "object") {
					for (const key of Object.keys(csp)) {
						if (typeof csp[key] === "string") {
							const sanitized = sanitizeCSP(csp[key]);

							if (sanitized !== csp[key]) {
								csp[key] = sanitized;
								modified = true;
							}
						}
					}
				}

				if (modified) {
					fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2));
					anyModified = true;
				}
			} catch {
				// Skip malformed manifests
			}
		}
	}

	return anyModified;
}
