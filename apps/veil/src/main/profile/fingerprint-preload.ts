import fs from "node:fs";
import path from "node:path";
import { app } from "electron";
import type { Fingerprint } from "../../stores/profile-store";

const PRELOAD_TEMPLATE = `
(function() {
	const fp = __PANE_FP_CONFIG__;

	const platformMap = { windows: "Win32", macos: "MacIntel", linux: "Linux x86_64" };
	const navPlatform = platformMap[fp.platform] || fp.platform;

	Object.defineProperty(navigator, "platform", { get: () => navPlatform });
	Object.defineProperty(navigator, "hardwareConcurrency", { get: () => fp.hardwareConcurrency });
	Object.defineProperty(navigator, "deviceMemory", { get: () => fp.deviceMemory });
	Object.defineProperty(navigator, "maxTouchPoints", { get: () => fp.maxTouchPoints });
	Object.defineProperty(navigator, "language", { get: () => fp.language });
	Object.defineProperty(navigator, "languages", { get: () => Object.freeze([...fp.languages]) });

	if (fp.screen) {
		Object.defineProperty(screen, "width", { get: () => fp.screen.width });
		Object.defineProperty(screen, "height", { get: () => fp.screen.height });
		Object.defineProperty(screen, "availWidth", { get: () => fp.screen.width });
		Object.defineProperty(screen, "availHeight", { get: () => fp.screen.height });
		if (fp.screen.colorDepth) {
			Object.defineProperty(screen, "colorDepth", { get: () => fp.screen.colorDepth });
			Object.defineProperty(screen, "pixelDepth", { get: () => fp.screen.colorDepth });
		}
	}

	if (fp.webgl) {
		const originalGetParameter = WebGLRenderingContext.prototype.getParameter;
		const UNMASKED_VENDOR = 0x9245;
		const UNMASKED_RENDERER = 0x9246;

		WebGLRenderingContext.prototype.getParameter = function(param) {
			if (param === UNMASKED_VENDOR) return fp.webgl.vendor;
			if (param === UNMASKED_RENDERER) return fp.webgl.renderer;
			return originalGetParameter.call(this, param);
		};

		if (typeof WebGL2RenderingContext !== "undefined") {
			const originalGetParameter2 = WebGL2RenderingContext.prototype.getParameter;

			WebGL2RenderingContext.prototype.getParameter = function(param) {
				if (param === UNMASKED_VENDOR) return fp.webgl.vendor;
				if (param === UNMASKED_RENDERER) return fp.webgl.renderer;
				return originalGetParameter2.call(this, param);
			};
		}
	}

	if (fp.canvas && fp.canvas.noise) {
		const seed = fp._profileHash || 0;
		function seededRandom(s) {
			s = (s * 1664525 + 1013904223) & 0xFFFFFFFF;
			return { next: s, value: (s >>> 16) / 65536 };
		}

		const originalToDataURL = HTMLCanvasElement.prototype.toDataURL;
		HTMLCanvasElement.prototype.toDataURL = function(...args) {
			const ctx = this.getContext("2d");
			if (ctx) {
				const imageData = ctx.getImageData(0, 0, this.width, this.height);
				let s = seed;
				for (let i = 0; i < imageData.data.length; i += 4) {
					const r = seededRandom(s + i);
					s = r.next;
					imageData.data[i] = (imageData.data[i] + (r.value < 0.5 ? -1 : 1)) & 0xFF;
				}
				ctx.putImageData(imageData, 0, 0);
			}
			return originalToDataURL.apply(this, args);
		};
	}

	if (fp.audio && fp.audio.noise) {
		const originalStartRendering = OfflineAudioContext.prototype.startRendering;

		OfflineAudioContext.prototype.startRendering = function() {
			return originalStartRendering.call(this).then(function(buffer) {
				const channel = buffer.getChannelData(0);
				for (let i = 0; i < channel.length; i++) {
					channel[i] += (Math.random() - 0.5) * 0.0001;
				}
				return buffer;
			});
		};
	}
})();
`;

export function generateFingerprintPreload(
	profileId: string,
	fingerprint: Fingerprint,
): string {
	const config = {
		...fingerprint,
		_profileHash: hashCode(profileId),
	};

	const content = PRELOAD_TEMPLATE.replace(
		"__PANE_FP_CONFIG__",
		JSON.stringify(config),
	);

	const tmpDir = path.join(app.getPath("temp"), "pane-fingerprints");

	if (!fs.existsSync(tmpDir)) {
		fs.mkdirSync(tmpDir, { recursive: true });
	}

	const filePath = path.join(tmpDir, `fp-${profileId}.js`);
	fs.writeFileSync(filePath, content, "utf-8");

	return filePath;
}

export function cleanupFingerprintPreload(profileId: string): void {
	const filePath = path.join(
		app.getPath("temp"),
		"pane-fingerprints",
		`fp-${profileId}.js`,
	);

	try {
		fs.unlinkSync(filePath);
	} catch {}
}

function hashCode(str: string): number {
	let hash = 0;

	for (let i = 0; i < str.length; i++) {
		hash = (hash * 31 + str.charCodeAt(i)) | 0;
	}

	return Math.abs(hash);
}
