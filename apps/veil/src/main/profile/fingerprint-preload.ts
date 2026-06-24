import fs from "node:fs";
import path from "node:path";
import { app } from "electron";
import type { Fingerprint } from "../../stores/profile-store";
import { deriveClientHints } from "./client-hints";

// The preload runs in the ISOLATED world; mutating its navigator/screen/WebGL
// prototypes there is invisible to the page. So it hands a fully self-contained
// spoof function to contextBridge.executeInMainWorld, which re-evaluates it in the
// page's MAIN world (before the page's own scripts run) — where the prototypes it
// patches are the ones the page actually reads. All inputs arrive via `args`
// because the function is serialised and loses its defining scope.
const PRELOAD_TEMPLATE = `
const { contextBridge } = require("electron");

const __PANE_FP__ = __PANE_FP_CONFIG__;

function __paneApplyFingerprint(fp) {
	// Camouflage: make every getter/method we patch report "[native code]" from
	// toString(), routed through one Proxy on Function.prototype.toString so the
	// proxy itself also reads as native.
	const nativeToString = Function.prototype.toString;
	const labels = new WeakMap();
	const asNative = (fn, label) => { labels.set(fn, label); return fn; };
	const toStringProxy = new Proxy(nativeToString, {
		apply(target, thisArg, args) {
			const label = labels.get(thisArg);
			if (label) return "function " + label + "() { [native code] }";
			return Reflect.apply(target, thisArg, args);
		},
	});
	labels.set(toStringProxy, "toString");
	Function.prototype.toString = toStringProxy;

	// Define on the prototype (where native getters live) and non-enumerable (as
	// native interface attributes are) so descriptor/hasOwnProperty/for-in probes
	// match a real browser.
	const defineGetter = (proto, prop, getter) => {
		Object.defineProperty(proto, prop, {
			get: asNative(getter, "get " + prop),
			configurable: true,
			enumerable: false,
		});
	};

	const platformMap = { WINDOWS: "Win32", MACOS: "MacIntel", LINUX: "Linux x86_64" };
	const navPlatform = platformMap[fp.platform] || fp.platform;
	const navProto = Object.getPrototypeOf(navigator);
	const screenProto = Object.getPrototypeOf(screen);

	defineGetter(navProto, "platform", () => navPlatform);
	defineGetter(navProto, "hardwareConcurrency", () => fp.hardwareConcurrency);
	defineGetter(navProto, "deviceMemory", () => fp.deviceMemory);
	defineGetter(navProto, "maxTouchPoints", () => fp.maxTouchPoints);
	defineGetter(navProto, "language", () => fp.language);
	defineGetter(navProto, "languages", () => Object.freeze(fp.languages.slice()));

	if (fp.screen) {
		defineGetter(screenProto, "width", () => fp.screen.width);
		defineGetter(screenProto, "height", () => fp.screen.height);
		defineGetter(screenProto, "availWidth", () => fp.screen.width);
		defineGetter(screenProto, "availHeight", () => fp.screen.height);
		if (fp.screen.colorDepth) {
			defineGetter(screenProto, "colorDepth", () => fp.screen.colorDepth);
			defineGetter(screenProto, "pixelDepth", () => fp.screen.colorDepth);
		}
	}

	// userAgentData consistent with the spoofed platform + UA. The values are shared
	// with the Sec-CH-UA-* header rewrite (client-hints.ts) so the JS and HTTP
	// surfaces can't be cross-checked against each other or against navigator.platform.
	const ch = fp._clientHints;
	const highEntropy = {
		architecture: ch.architecture,
		bitness: ch.bitness,
		brands: ch.brands,
		fullVersionList: ch.fullVersionList,
		mobile: ch.mobile,
		model: "",
		platform: ch.platform,
		platformVersion: ch.platformVersion,
		uaFullVersion: ch.uaFullVersion,
		wow64: false,
	};
	const uaData = {
		brands: ch.brands,
		mobile: ch.mobile,
		platform: ch.platform,
		getHighEntropyValues: asNative(function getHighEntropyValues(hints) {
			const result = { brands: ch.brands, mobile: ch.mobile, platform: ch.platform };
			if (Array.isArray(hints)) {
				for (const hint of hints) {
					if (hint in highEntropy) result[hint] = highEntropy[hint];
				}
			}
			return Promise.resolve(result);
		}, "getHighEntropyValues"),
		toJSON: asNative(function toJSON() {
			return { brands: ch.brands, mobile: ch.mobile, platform: ch.platform };
		}, "toJSON"),
	};
	if (typeof NavigatorUAData !== "undefined") {
		Object.setPrototypeOf(uaData, NavigatorUAData.prototype);
	}
	defineGetter(navProto, "userAgentData", () => uaData);

	if (fp.webgl) {
		const UNMASKED_VENDOR = 0x9245;
		const UNMASKED_RENDERER = 0x9246;
		const patchGetParameter = (proto) => {
			const original = proto.getParameter;
			proto.getParameter = asNative(function getParameter(param) {
				if (param === UNMASKED_VENDOR) return fp.webgl.vendor;
				if (param === UNMASKED_RENDERER) return fp.webgl.renderer;
				return original.call(this, param);
			}, "getParameter");
		};
		if (typeof WebGLRenderingContext !== "undefined") {
			patchGetParameter(WebGLRenderingContext.prototype);
		}
		if (typeof WebGL2RenderingContext !== "undefined") {
			patchGetParameter(WebGL2RenderingContext.prototype);
		}
	}

	if (fp.canvas && fp.canvas.noise && typeof HTMLCanvasElement !== "undefined") {
		const seed = fp._profileHash || 0;
		const originalToDataURL = HTMLCanvasElement.prototype.toDataURL;
		HTMLCanvasElement.prototype.toDataURL = asNative(function toDataURL(...args) {
			const context = this.getContext("2d");
			if (context) {
				const imageData = context.getImageData(0, 0, this.width, this.height);
				let state = seed;
				for (let i = 0; i < imageData.data.length; i += 4) {
					state = (state * 1664525 + 1013904223) & 0xffffffff;
					const delta = (state >>> 16) / 65536 < 0.5 ? -1 : 1;
					imageData.data[i] = (imageData.data[i] + delta) & 0xff;
				}
				context.putImageData(imageData, 0, 0);
			}
			return originalToDataURL.apply(this, args);
		}, "toDataURL");
	}

	if (fp.audio && fp.audio.noise && typeof OfflineAudioContext !== "undefined") {
		const originalStartRendering = OfflineAudioContext.prototype.startRendering;
		OfflineAudioContext.prototype.startRendering = asNative(function startRendering() {
			return originalStartRendering.call(this).then(function (buffer) {
				const channel = buffer.getChannelData(0);
				for (let i = 0; i < channel.length; i++) {
					channel[i] += (Math.random() - 0.5) * 0.0001;
				}
				return buffer;
			});
		}, "startRendering");
	}
}

try {
	contextBridge.executeInMainWorld({
		func: __paneApplyFingerprint,
		args: [__PANE_FP__],
	});
} catch (_error) {
	// executeInMainWorld unavailable (older Electron / no context isolation):
	// run in the current world. It can't reach the page, but it never throws.
	__paneApplyFingerprint(__PANE_FP__);
}
`;

export function generateFingerprintPreload(
	profileId: string,
	fingerprint: Fingerprint,
): string {
	const config = {
		...fingerprint,
		_profileHash: hashCode(profileId),
		_clientHints: deriveClientHints(fingerprint),
	};

	const content = PRELOAD_TEMPLATE.replace("__PANE_FP_CONFIG__", () =>
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
