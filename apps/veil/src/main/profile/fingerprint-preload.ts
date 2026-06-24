import fs from "node:fs";
import path from "node:path";
import { app } from "electron";
import type { Fingerprint } from "../../stores/profile-store";
import { type ClientHints, deriveClientHints } from "./client-hints";

// In a frame this preload runs in the ISOLATED world, so it hands the spoof to
// contextBridge.executeInMainWorld to run in the page's MAIN world (before page
// scripts). In a service worker there is no contextBridge, so the same function
// runs directly in the worker's global scope. The function is worker-safe (every
// DOM API is typeof-guarded) and self-contained (inputs arrive via `args`, since
// executeInMainWorld serialises it and drops its defining scope).
const PRELOAD_TEMPLATE = `
const __PANE_FP__ = __PANE_FP_CONFIG__;

function __paneApplyFingerprint(fp) {
	const ch = fp._clientHints;

	// Camouflage: patched getters/methods report "[native code]" from toString()
	// and carry the right .name, routed through one Proxy on
	// Function.prototype.toString so the proxy itself also reads as native.
	const nativeToString = Function.prototype.toString;
	const labels = new WeakMap();
	const asNative = (fn, label) => {
		Object.defineProperty(fn, "name", { value: label, configurable: true });
		labels.set(fn, label);
		return fn;
	};
	const toStringProxy = new Proxy(nativeToString, {
		apply(target, thisArg, args) {
			const label = labels.get(thisArg);
			if (label) return "function " + label + "() { [native code] }";
			return Reflect.apply(target, thisArg, args);
		},
	});
	labels.set(toStringProxy, "toString");
	Function.prototype.toString = toStringProxy;

	// Native DOM attributes live on the prototype, non-enumerable.
	const defineGetter = (proto, prop, getter) => {
		Object.defineProperty(proto, prop, {
			get: asNative(getter, "get " + prop),
			configurable: true,
			enumerable: false,
		});
	};

	// Replace a native method, preserving its descriptor flags; makeWrapper gets the
	// original so it can delegate the non-spoofed path.
	const patchMethod = (obj, prop, makeWrapper) => {
		const descriptor = Object.getOwnPropertyDescriptor(obj, prop);
		if (!descriptor || typeof descriptor.value !== "function") return;
		const wrapper = asNative(makeWrapper(descriptor.value), prop);
		Object.defineProperty(obj, prop, Object.assign({}, descriptor, { value: wrapper }));
	};

	const navProto = Object.getPrototypeOf(navigator);
	defineGetter(navProto, "platform", () => fp._navPlatform);
	defineGetter(navProto, "hardwareConcurrency", () => fp.hardwareConcurrency);
	defineGetter(navProto, "deviceMemory", () => fp.deviceMemory);
	defineGetter(navProto, "maxTouchPoints", () => fp.maxTouchPoints);
	defineGetter(navProto, "language", () => fp.language);
	defineGetter(navProto, "languages", () => Object.freeze(fp.languages.slice()));

	if (fp.screen && typeof screen !== "undefined") {
		// availWidth/availHeight leave room for the OS chrome (taskbar / menu bar);
		// reporting the full screen size is a tell.
		const inset = { WINDOWS: 48, MACOS: 25, LINUX: 27 }[fp.platform] || 0;
		const screenProto = Object.getPrototypeOf(screen);
		defineGetter(screenProto, "width", () => fp.screen.width);
		defineGetter(screenProto, "height", () => fp.screen.height);
		defineGetter(screenProto, "availWidth", () => fp.screen.width);
		defineGetter(screenProto, "availHeight", () => fp.screen.height - inset);
		if (fp.screen.colorDepth) {
			defineGetter(screenProto, "colorDepth", () => fp.screen.colorDepth);
			defineGetter(screenProto, "pixelDepth", () => fp.screen.colorDepth);
		}
	}

	// navigator.userAgentData — consistent with the Sec-CH-UA-* header rewrite (both
	// from client-hints.ts). Override the real NavigatorUAData prototype when present
	// so navigator.userAgentData stays a genuine instance; else synthesise one.
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
	function getHighEntropyValues(hints) {
		const result = { brands: ch.brands, mobile: ch.mobile, platform: ch.platform };
		if (Array.isArray(hints)) {
			for (const hint of hints) {
				if (hint in highEntropy) result[hint] = highEntropy[hint];
			}
		}
		return Promise.resolve(result);
	}
	function toJSON() {
		return { brands: ch.brands, mobile: ch.mobile, platform: ch.platform };
	}

	if (typeof NavigatorUAData !== "undefined" && navigator.userAgentData) {
		const uaProto = NavigatorUAData.prototype;
		defineGetter(uaProto, "brands", () => ch.brands);
		defineGetter(uaProto, "mobile", () => ch.mobile);
		defineGetter(uaProto, "platform", () => ch.platform);
		patchMethod(uaProto, "getHighEntropyValues", () => getHighEntropyValues);
		patchMethod(uaProto, "toJSON", () => toJSON);
	} else {
		const base = typeof NavigatorUAData !== "undefined" ? NavigatorUAData.prototype : Object.prototype;
		const uaProto = Object.create(base);
		defineGetter(uaProto, "brands", () => ch.brands);
		defineGetter(uaProto, "mobile", () => ch.mobile);
		defineGetter(uaProto, "platform", () => ch.platform);
		Object.defineProperty(uaProto, "getHighEntropyValues", {
			value: asNative(getHighEntropyValues, "getHighEntropyValues"),
			writable: true, enumerable: false, configurable: true,
		});
		Object.defineProperty(uaProto, "toJSON", {
			value: asNative(toJSON, "toJSON"),
			writable: true, enumerable: false, configurable: true,
		});
		defineGetter(navProto, "userAgentData", () => Object.create(uaProto));
	}

	if (fp.webgl) {
		const UNMASKED_VENDOR = 0x9245;
		const UNMASKED_RENDERER = 0x9246;
		const makeGetParameter = (original) => function getParameter(param) {
			if (param === UNMASKED_VENDOR) return fp.webgl.vendor;
			if (param === UNMASKED_RENDERER) return fp.webgl.renderer;
			return original.call(this, param);
		};
		if (typeof WebGLRenderingContext !== "undefined") {
			patchMethod(WebGLRenderingContext.prototype, "getParameter", makeGetParameter);
		}
		if (typeof WebGL2RenderingContext !== "undefined") {
			patchMethod(WebGL2RenderingContext.prototype, "getParameter", makeGetParameter);
		}
	}

	// Canvas noise: a position-keyed PRNG (not a sequential walk) so a direct
	// getImageData read of any sub-region produces the SAME ±1 per channel as the
	// full-canvas noise used by toDataURL/toBlob — otherwise the two surfaces
	// disagree and lie-detectors (creepjs) flag the canvas.
	if (fp.canvas && fp.canvas.noise && typeof CanvasRenderingContext2D !== "undefined") {
		const seed = (fp._profileHash || 0) >>> 0;
		const deltaAt = (index) => {
			let h = (seed ^ index) >>> 0;
			h = Math.imul(h ^ (h >>> 16), 0x45d9f3b);
			h = Math.imul(h ^ (h >>> 16), 0x45d9f3b);
			h = (h ^ (h >>> 16)) >>> 0;
			return h / 4294967296 < 0.5 ? -1 : 1;
		};
		const noiseRegion = (imageData, originX, originY, fullWidth) => {
			const data = imageData.data;
			for (let row = 0; row < imageData.height; row++) {
				for (let col = 0; col < imageData.width; col++) {
					const absolute = ((originY + row) * fullWidth + (originX + col)) * 3;
					const di = (row * imageData.width + col) * 4;
					data[di] = data[di] + deltaAt(absolute);
					data[di + 1] = data[di + 1] + deltaAt(absolute + 1);
					data[di + 2] = data[di + 2] + deltaAt(absolute + 2);
				}
			}
		};
		const rawGetImageData = CanvasRenderingContext2D.prototype.getImageData;
		const applyNoise = (canvas) => {
			const context = canvas.getContext && canvas.getContext("2d");
			if (!context || !canvas.width || !canvas.height) return null;
			const original = rawGetImageData.call(context, 0, 0, canvas.width, canvas.height);
			const noised = rawGetImageData.call(context, 0, 0, canvas.width, canvas.height);
			noiseRegion(noised, 0, 0, canvas.width);
			context.putImageData(noised, 0, 0);
			return () => context.putImageData(original, 0, 0);
		};
		// Direct pixel reads are noised in place (using the raw reader to avoid double
		// application) so they match what toDataURL/toBlob encode.
		patchMethod(CanvasRenderingContext2D.prototype, "getImageData", (original) => function getImageData(sx, sy, sw, sh) {
			const imageData = original.call(this, sx, sy, sw, sh);
			noiseRegion(imageData, sx, sy, this.canvas.width);
			return imageData;
		});
		if (typeof HTMLCanvasElement !== "undefined") {
			patchMethod(HTMLCanvasElement.prototype, "toDataURL", (original) => function toDataURL(...args) {
				const restore = applyNoise(this);
				const result = original.apply(this, args);
				if (restore) restore();
				return result;
			});
			patchMethod(HTMLCanvasElement.prototype, "toBlob", (original) => function toBlob(callback, ...args) {
				const restore = applyNoise(this);
				const done = (blob) => {
					if (restore) restore();
					if (typeof callback === "function") callback(blob);
				};
				return original.call(this, done, ...args);
			});
		}
		if (typeof OffscreenCanvas !== "undefined") {
			patchMethod(OffscreenCanvas.prototype, "convertToBlob", (original) => function convertToBlob(...args) {
				const restore = applyNoise(this);
				return Promise.resolve(original.apply(this, args)).then((blob) => {
					if (restore) restore();
					return blob;
				});
			});
		}
	}

	// Audio noise: seeded per-sample delta on every channel (deterministic per
	// profile, unlike Math.random which would diverge across identical renders).
	if (fp.audio && fp.audio.noise && typeof OfflineAudioContext !== "undefined") {
		patchMethod(OfflineAudioContext.prototype, "startRendering", (original) => function startRendering() {
			return original.call(this).then(function (buffer) {
				for (let c = 0; c < buffer.numberOfChannels; c++) {
					const channel = buffer.getChannelData(c);
					let state = ((fp._profileHash ^ 0x5bd1e995) + c * 0x9e3779b9) >>> 0;
					for (let i = 0; i < channel.length; i++) {
						state = (state * 1664525 + 1013904223) & 0xffffffff;
						channel[i] += ((state >>> 16) / 65536 - 0.5) * 0.0001;
					}
				}
				return buffer;
			});
		});
	}

	// Timezone: report fp.timezone from Intl AND the Date string/offset methods
	// (which read ICU directly and bypass the Intl.DateTimeFormat constructor).
	if (fp.timezone && typeof Intl !== "undefined") {
		const OriginalDTF = Intl.DateTimeFormat;
		const partsFormat = new OriginalDTF("en-US", {
			timeZone: fp.timezone, hourCycle: "h23",
			weekday: "short", year: "numeric", month: "short", day: "2-digit",
			hour: "2-digit", minute: "2-digit", second: "2-digit",
		});
		const offsetFormat = new OriginalDTF("en-US", { timeZone: fp.timezone, timeZoneName: "longOffset" });
		const longNameFormat = new OriginalDTF("en-US", { timeZone: fp.timezone, timeZoneName: "long" });
		const partsOf = (date, format) => {
			const out = {};
			for (const part of format.formatToParts(date)) out[part.type] = part.value;
			return out;
		};
		const gmtOf = (date) => {
			const raw = (partsOf(date, offsetFormat).timeZoneName || "").replace(":", "");
			return /^GMT[+-]\\d{4}$/.test(raw) ? raw : "GMT+0000";
		};
		const nameOf = (date) => partsOf(date, longNameFormat).timeZoneName || "";

		const dtfProxy = new Proxy(OriginalDTF, {
			construct(target, args) {
				const options = Object.assign({}, args[1]);
				if (!options.timeZone) options.timeZone = fp.timezone;
				return Reflect.construct(target, [args[0], options]);
			},
			apply(target, thisArg, args) {
				const options = Object.assign({}, args[1]);
				if (!options.timeZone) options.timeZone = fp.timezone;
				return Reflect.apply(target, thisArg, [args[0], options]);
			},
		});
		OriginalDTF.prototype.constructor = dtfProxy;
		labels.set(dtfProxy, "DateTimeFormat");
		Intl.DateTimeFormat = dtfProxy;

		patchMethod(Date.prototype, "getTimezoneOffset", () => function getTimezoneOffset() {
			const match = gmtOf(this).match(/GMT([+-])(\\d{2})(\\d{2})/);
			if (!match) return 0;
			return ((match[1] === "-" ? 1 : -1) * (parseInt(match[2], 10) * 60 + parseInt(match[3], 10))) || 0;
		});
		patchMethod(Date.prototype, "toString", () => function toString() {
			if (Number.isNaN(this.getTime())) return "Invalid Date";
			const p = partsOf(this, partsFormat);
			return p.weekday + " " + p.month + " " + p.day + " " + p.year + " " + p.hour + ":" + p.minute + ":" + p.second + " " + gmtOf(this) + " (" + nameOf(this) + ")";
		});
		patchMethod(Date.prototype, "toDateString", () => function toDateString() {
			if (Number.isNaN(this.getTime())) return "Invalid Date";
			const p = partsOf(this, partsFormat);
			return p.weekday + " " + p.month + " " + p.day + " " + p.year;
		});
		patchMethod(Date.prototype, "toTimeString", () => function toTimeString() {
			if (Number.isNaN(this.getTime())) return "Invalid Date";
			const p = partsOf(this, partsFormat);
			return p.hour + ":" + p.minute + ":" + p.second + " " + gmtOf(this) + " (" + nameOf(this) + ")";
		});
		const patchLocale = (method, defaults) => patchMethod(Date.prototype, method, () => function (locales, options) {
			if (Number.isNaN(this.getTime())) return "Invalid Date";
			const opts = options ? Object.assign({}, options) : Object.assign({}, defaults);
			if (!opts.timeZone) opts.timeZone = fp.timezone;
			return new OriginalDTF(locales, opts).format(this);
		});
		patchLocale("toLocaleString", { year: "numeric", month: "numeric", day: "numeric", hour: "numeric", minute: "numeric", second: "numeric" });
		patchLocale("toLocaleDateString", { year: "numeric", month: "numeric", day: "numeric" });
		patchLocale("toLocaleTimeString", { hour: "numeric", minute: "numeric", second: "numeric" });
	}
}

let __paneBridge;
try {
	__paneBridge = require("electron").contextBridge;
} catch (_error) {
	__paneBridge = undefined;
}

if (__paneBridge && typeof __paneBridge.executeInMainWorld === "function") {
	__paneBridge.executeInMainWorld({ func: __paneApplyFingerprint, args: [__PANE_FP__] });
} else {
	// Service-worker / no-bridge context: run directly in this global scope.
	__paneApplyFingerprint(__PANE_FP__);
}
`;

const NAV_PLATFORM: Record<string, string> = {
	WINDOWS: "Win32",
	MACOS: "MacIntel",
	LINUX: "Linux x86_64",
};

interface FingerprintConfig extends Fingerprint {
	_profileHash: number;
	_navPlatform: string;
	_clientHints: ClientHints;
}

export function generateFingerprintPreload(
	profileId: string,
	fingerprint: Fingerprint,
): string {
	const config: FingerprintConfig = {
		...fingerprint,
		_profileHash: hashCode(profileId),
		_navPlatform: NAV_PLATFORM[fingerprint.platform] ?? fingerprint.platform,
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
	try {
		fs.unlinkSync(
			path.join(app.getPath("temp"), "pane-fingerprints", `fp-${profileId}.js`),
		);
	} catch {
		// Best-effort: the preload file may never have been written.
	}
}

function hashCode(str: string): number {
	let hash = 0;

	for (let i = 0; i < str.length; i++) {
		hash = (hash * 31 + str.charCodeAt(i)) | 0;
	}

	return Math.abs(hash);
}
