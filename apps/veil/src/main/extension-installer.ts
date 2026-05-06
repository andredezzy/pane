import nodeFs from "node:fs";
import fs from "node:fs/promises";
import path from "node:path";
import { type Extension, net, protocol, session } from "electron";
import {
	installExtension,
	uninstallExtension,
	updateExtensions,
} from "electron-chrome-web-store";

import type { Profile } from "./profile/profile";

export interface InstalledExtension {
	id: string;
	name: string;
	version: string;
	description: string;
	icon: string;
}

interface ExtensionInstallerHost {
	readonly profiles: ReadonlyMap<string, Profile>;
}

export class ExtensionInstaller {
	static registerProtocol(extensionsPath: string): void {
		protocol.handle("pane-extension", (request) => {
			const url = new URL(request.url);
			const extensionId = url.hostname;

			const extDir = path.join(extensionsPath, extensionId);

			if (!nodeFs.existsSync(extDir)) {
				return new Response("Not found", { status: 404 });
			}

			const versions = nodeFs.readdirSync(extDir);

			if (versions.length === 0) {
				return new Response("Not found", { status: 404 });
			}

			const sorted = [...versions].sort((a, b) => {
				const pa = a.split(".").map(Number);
				const pb = b.split(".").map(Number);
				for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
					const diff = (pa[i] ?? 0) - (pb[i] ?? 0);

					if (diff !== 0) {
						return diff;
					}
				}

				return 0;
			});

			const versionDir = path.join(extDir, sorted[sorted.length - 1]);
			const manifestPath = path.join(versionDir, "manifest.json");

			if (!nodeFs.existsSync(manifestPath)) {
				return new Response("Not found", { status: 404 });
			}

			const manifest = JSON.parse(
				nodeFs.readFileSync(manifestPath, "utf-8"),
			) as {
				icons?: Record<string, string>;
			};

			const icons = manifest.icons;

			if (!icons) {
				return new Response("Not found", { status: 404 });
			}

			const largest = Object.keys(icons)
				.map(Number)
				.sort((a, b) => b - a)[0];

			if (!largest || !icons[String(largest)]) {
				return new Response("Not found", { status: 404 });
			}

			const iconPath = path.join(versionDir, icons[String(largest)]);
			const resolved = path.resolve(iconPath);

			if (!resolved.startsWith(path.resolve(extensionsPath) + path.sep)) {
				return new Response("Forbidden", { status: 403 });
			}

			return net.fetch(`file://${resolved}`);
		});
	}

	constructor(
		private readonly host: ExtensionInstallerHost,
		private readonly extensionsPath: string,
	) {}

	async install(extensionId: string): Promise<Extension | null> {
		try {
			const ext = await installExtension(extensionId, {
				extensionsPath: this.extensionsPath,
			});

			for (const profile of this.host.profiles.values()) {
				await profile.extensions.loadOne(extensionId);
			}

			return ext;
		} catch (err) {
			console.error(`[CWS] Failed to install ${extensionId}:`, err);

			return null;
		}
	}

	async uninstall(extensionId: string): Promise<void> {
		for (const profile of this.host.profiles.values()) {
			profile.extensions.unload(extensionId);
		}

		await uninstallExtension(extensionId, {
			extensionsPath: this.extensionsPath,
		});
	}

	async getInstalled(): Promise<InstalledExtension[]> {
		const result: InstalledExtension[] = [];

		try {
			await fs.access(this.extensionsPath);
		} catch {
			return result;
		}

		for (const extId of await fs.readdir(this.extensionsPath)) {
			const extDir = path.join(this.extensionsPath, extId);

			if (!(await fs.stat(extDir)).isDirectory()) {
				continue;
			}

			for (const version of await fs.readdir(extDir)) {
				const manifestPath = path.join(extDir, version, "manifest.json");

				try {
					const manifest = JSON.parse(await fs.readFile(manifestPath, "utf-8"));
					let name: string = manifest.name ?? extId;

					if (name.startsWith("__MSG_") && name.endsWith("__")) {
						const msgKey = name.slice(6, -2);

						try {
							const messagesPath = path.join(
								extDir,
								version,
								"_locales",
								"en",
								"messages.json",
							);

							const messages = JSON.parse(
								await fs.readFile(messagesPath, "utf-8"),
							);

							name = messages[msgKey]?.message ?? name;
						} catch {}
					}

					let description: string = manifest.description ?? "";

					if (description.startsWith("__MSG_") && description.endsWith("__")) {
						const msgKey = description.slice(6, -2);

						try {
							const messagesPath = path.join(
								extDir,
								version,
								"_locales",
								"en",
								"messages.json",
							);

							const messages = JSON.parse(
								await fs.readFile(messagesPath, "utf-8"),
							);

							description = messages[msgKey]?.message ?? description;
						} catch {}
					}

					const icons: Record<string, string> | undefined = manifest.icons;
					let icon = "";

					if (icons) {
						const largest = Object.keys(icons)
							.map(Number)
							.sort((a, b) => b - a)[0];

						if (largest) {
							icon = `pane-extension://${extId}/icon`;
						}
					}

					result.push({
						id: extId,
						name,
						version: manifest.version ?? version,
						description,
						icon,
					});
				} catch (err) {
					console.warn(`[CWS] Skipping extension ${extId}/${version}:`, err);
				}
			}
		}

		return result;
	}

	async checkForUpdates(): Promise<void> {
		try {
			const updateSession = session.fromPartition("persist:pane-internal");
			await updateExtensions(updateSession);
		} catch (err) {
			console.error("[CWS] Update check failed:", err);
		}
	}
}
