import fs from "node:fs";
import path from "node:path";
import type { Extension, Session } from "electron";
import { loadAllExtensions } from "electron-chrome-web-store";
import { sanitizeExtensionManifests } from "./sanitize-manifests";

export interface ExtensionRuntimeOptions {
	profileId: string;
	session: Session;
	extensionsPath: string;
	onExtensionLoaded?: (
		profileId: string,
		ext: { id: string; name: string; version: string },
	) => void;
}

function latestVersion(versions: string[]): string | undefined {
	return versions
		.sort((a, b) => {
			const pa = a.split(".").map(Number);
			const pb = b.split(".").map(Number);
			for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
				const diff = (pa[i] ?? 0) - (pb[i] ?? 0);

				if (diff !== 0) {
					return diff;
				}
			}

			return 0;
		})
		.at(-1);
}

export class ExtensionRuntime {
	private static readonly MAX_RETRIES = 3;
	private loadPromise: Promise<void> | null = null;
	private retryCount = 0;

	constructor(private readonly opts: ExtensionRuntimeOptions) {
		// Electron persist sessions auto-restore extensions using cached (unsanitized)
		// manifests. Remove them so they get re-loaded from sanitized manifests on disk.
		const cached = opts.session.extensions.getAllExtensions();

		if (cached.length > 0) {
			sanitizeExtensionManifests(opts.extensionsPath);
			for (const ext of cached) {
				opts.session.extensions.removeExtension(ext.id);
			}
		}
	}

	ensureLoaded(): Promise<void> {
		if (this.loadPromise) {
			return this.loadPromise;
		}

		if (this.retryCount >= ExtensionRuntime.MAX_RETRIES) {
			return Promise.resolve();
		}

		this.loadPromise = this.load().catch((err) => {
			this.loadPromise = null;
			this.retryCount++;

			console.error(
				`[ExtensionRuntime] Load failed (attempt ${this.retryCount}/${ExtensionRuntime.MAX_RETRIES}):`,
				err,
			);
		});

		return this.loadPromise;
	}

	private async load(): Promise<void> {
		sanitizeExtensionManifests(this.opts.extensionsPath);
		await loadAllExtensions(this.opts.session, this.opts.extensionsPath);
		const loaded = this.opts.session.extensions.getAllExtensions();
		for (const ext of loaded) {
			this.opts.onExtensionLoaded?.(this.opts.profileId, {
				id: ext.id,
				name: ext.name,
				version: ext.manifest.version,
			});
		}
	}

	async loadOne(extensionId: string): Promise<void> {
		const alreadyLoaded = this.opts.session.extensions.getAllExtensions();

		if (alreadyLoaded.some((ext) => ext.id === extensionId)) {
			return;
		}

		const extDir = path.join(this.opts.extensionsPath, extensionId);

		if (!fs.existsSync(extDir)) {
			return;
		}

		const versions = fs.readdirSync(extDir);

		if (versions.length === 0) {
			return;
		}

		const latest = latestVersion(versions);

		if (!latest) {
			return;
		}

		sanitizeExtensionManifests(this.opts.extensionsPath);

		const extPath = path.join(extDir, latest);
		const ext = await this.opts.session.loadExtension(extPath);

		this.opts.onExtensionLoaded?.(this.opts.profileId, {
			id: ext.id,
			name: ext.name,
			version: ext.manifest.version,
		});
	}

	unload(extensionId: string): void {
		this.opts.session.extensions.removeExtension(extensionId);
	}

	getLoaded(): Extension[] {
		return this.opts.session.extensions.getAllExtensions();
	}
}
