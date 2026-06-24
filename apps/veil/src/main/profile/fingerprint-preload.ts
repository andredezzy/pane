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
	const ch = fp._clientHints;

	// Camouflage: patched getters/methods report "[native code]" from toString()
	// (and carry the right .name), routed through one Proxy on
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

	// Replace a native method while preserving its descriptor flags; makeWrapper
	// receives the original so it can delegate the non-spoofed path.
	const patchMethod = (obj, prop, makeWrapper) => {
		const descriptor = Object.getOwnPropertyDescriptor(obj, prop);
		if (!descriptor || typeof descriptor.value !== "function") return;
		const wrapper = asNative(makeWrapper(descriptor.value), prop);
		Object.defineProperty(obj, prop, Object.assign({}, descriptor, { value: wrapper }));
	};

	const platformMap = { WINDOWS: "Win32", MACOS: "MacIntel", LINUX: "Linux x86_64" };
	const navProto = Object.getPrototypeOf(navigator);
	const screenProto = Object.getPrototypeOf(screen);

	defineGetter(navProto, "platform", () => platformMap[fp.platform] || fp.platform);
	defineGetter(navProto, "hardwareConcurrency", () => fp.hardwareConcurrency);
	defineGetter(navProto, "deviceMemory", () => fp.deviceMemory);
	defineGetter(navProto, "maxTouchPoints", () => fp.maxTouchPoints);
	defineGetter(navProto, "language", () => fp.language);
	defineGetter(navProto, "languages", () => Object.freeze(fp.languages.slice()));

	if (fp.screen) {
		// availWidth/availHeight leave room for the OS chrome (taskbar / menu bar);
		// reporting the full screen size is a tell.
		const inset = { WINDOWS: 48, MACOS: 25, LINUX: 27 }[fp.platform] || 0;
		defineGetter(screenProto, "width", () => fp.screen.width);
		defineGetter(screenProto, "height", () => fp.screen.height);
		defineGetter(screenProto, "availWidth", () => fp.screen.width);
		defineGetter(screenProto, "availHeight", () => fp.screen.height - inset);
		if (fp.screen.colorDepth) {
			defineGetter(screenProto, "colorDepth", () => fp.screen.colorDepth);
			defineGetter(screenProto, "pixelDepth", () => fp.screen.colorDepth);
		}
	}

	// navigator.userAgentData — kept consistent with the Sec-CH-UA-* header rewrite
	// (both derive from client-hints.ts). When the real NavigatorUAData exists we
	// override its prototype so navigator.userAgentData stays a genuine instance
	// (passing instanceof / getPrototypeOf / hasOwnProperty probes); otherwise we
	// synthesise a plausible object.
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
	const makeHighEntropy = () => function getHighEntropyValues(hints) {
		const result = { brands: ch.brands, mobile: ch.mobile, platform: ch.platform };
		if (Array.isArray(hints)) {
			for (const hint of hints) {
				if (hint in highEntropy) result[hint] = highEntropy[hint];
			}
		}
		return Promise.resolve(result);
	};
	const makeToJSON = () => function toJSON() {
		return { brands: ch.brands, mobile: ch.mobile, platform: ch.platform };
	};

	if (typeof NavigatorUAData !== "undefined" && navigator.userAgentData) {
		const uaProto = NavigatorUAData.prototype;
		defineGetter(uaProto, "brands", () => ch.brands);
		defineGetter(uaProto, "mobile", () => ch.mobile);
		defineGetter(uaProto, "platform", () => ch.platform);
		patchMethod(uaProto, "getHighEntropyValues", makeHighEntropy);
		patchMethod(uaProto, "toJSON", makeToJSON);
	} else {
		const uaData = Object.create(
			typeof NavigatorUAData !== "undefined" ? NavigatorUAData.prototype : Object.prototype,
		);
		defineGetter(uaData, "brands", () => ch.brands);
		defineGetter(uaData, "mobile", () => ch.mobile);
		defineGetter(uaData, "platform", () => ch.platform);
		Object.defineProperty(uaData, "getHighEntropyValues", {
			value: asNative(makeHighEntropy(), "getHighEntropyValues"),
			writable: true, enumerable: false, configurable: true,
		});
		Object.defineProperty(uaData, "toJSON", {
			value: asNative(makeToJSON(), "toJSON"),
			writable: true, enumerable: false, configurable: true,
		});
		defineGetter(navProto, "userAgentData", () => uaData);
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

	// Canvas noise: seeded ±1 per RGB channel, applied to a temporary copy and then
	// restored, so toDataURL/toBlob stay idempotent and non-destructive (a real
	// browser never mutates the canvas on read, and returns identical bytes twice).
	if (fp.canvas && fp.canvas.noise && typeof HTMLCanvasElement !== "undefined") {
		const applyNoise = (canvas) => {
			const context = canvas.getContext && canvas.getContext("2d");
			if (!context || !canvas.width || !canvas.height) return null;
			const original = context.getImageData(0, 0, canvas.width, canvas.height);
			const noised = context.getImageData(0, 0, canvas.width, canvas.height);
			let state = fp._profileHash || 0;
			const data = noised.data;
			for (let i = 0; i < data.length; i += 4) {
				for (let c = 0; c < 3; c++) {
					state = (state * 1664525 + 1013904223) & 0xffffffff;
					data[i + c] = data[i + c] + ((state >>> 16) / 65536 < 0.5 ? -1 : 1);
				}
			}
			context.putImageData(noised, 0, 0);
			return () => context.putImageData(original, 0, 0);
		};
		patchMethod(HTMLCanvasElement.prototype, "toDataURL", (original) => function toDataURL(...args) {
			const restore = applyNoise(this);
			const result = original.apply(this, args);
			if (restore) restore();
			return result;
		});
		patchMethod(HTMLCanvasElement.prototype, "toBlob", (original) => function toBlob(callback, ...args) {
			const restore = applyNoise(this);
			const wrapped = restore && typeof callback === "function"
				? function (blob) { restore(); callback(blob); }
				: callback;
			return original.call(this, wrapped, ...args);
		});
	}

	// Audio noise: seeded per-sample ±tiny delta (deterministic per profile, unlike
	// Math.random which would diverge across renders of identical audio).
	if (fp.audio && fp.audio.noise && typeof OfflineAudioContext !== "undefined") {
		patchMethod(OfflineAudioContext.prototype, "startRendering", (original) => function startRendering() {
			return original.call(this).then(function (buffer) {
				const channel = buffer.getChannelData(0);
				let state = (fp._profileHash ^ 0x5bd1e995) >>> 0;
				for (let i = 0; i < channel.length; i++) {
					state = (state * 1664525 + 1013904223) & 0xffffffff;
					channel[i] += ((state >>> 16) / 65536 - 0.5) * 0.0001;
				}
				return buffer;
			});
		});
	}

	// Timezone: report fp.timezone from Intl and Date so it can't be cross-checked
	// against the spoofed locale / platform / proxy IP.
	if (fp.timezone) {
		const OriginalDTF = Intl.DateTimeFormat;
		const offsetFormat = new OriginalDTF("en-US", {
			timeZone: fp.timezone,
			timeZoneName: "longOffset",
		});
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
		labels.set(dtfProxy, "DateTimeFormat");
		Intl.DateTimeFormat = dtfProxy;
		patchMethod(Date.prototype, "getTimezoneOffset", () => function getTimezoneOffset() {
			const parts = offsetFormat.formatToParts(this);
			let name = "GMT+00:00";
			for (const part of parts) {
				if (part.type === "timeZoneName") name = part.value;
			}
			const match = name.match(/GMT([+-])(\\d{2}):?(\\d{2})?/);
			if (!match) return 0;
			return (match[1] === "-" ? 1 : -1) * (parseInt(match[2], 10) * 60 + parseInt(match[3] || "0", 10));
		});
	}
}

if (typeof contextBridge !== "undefined" && typeof contextBridge.executeInMainWorld === "function") {
	contextBridge.executeInMainWorld({ func: __paneApplyFingerprint, args: [__PANE_FP__] });
} else {
	// No main-world bridge (older Electron / no context isolation): apply directly.
	// It can't reach the page in that mode, but it never throws.
	__paneApplyFingerprint(__PANE_FP__);
}
`;

// Service-worker context spoof: workers have no DOM / executeInMainWorld, so this
// patches WorkerNavigator.prototype directly in the worker's own global scope.
const WORKER_TEMPLATE = `
const fp = __PANE_FP_CONFIG__;

const platformMap = { WINDOWS: "Win32", MACOS: "MacIntel", LINUX: "Linux x86_64" };
const proto = Object.getPrototypeOf(navigator);
const define = (prop, value) =>
	Object.defineProperty(proto, prop, { get: () => value, configurable: true, enumerable: false });

define("platform", platformMap[fp.platform] || fp.platform);
define("hardwareConcurrency", fp.hardwareConcurrency);
define("deviceMemory", fp.deviceMemory);
define("language", fp.language);
define("languages", Object.freeze(fp.languages.slice()));
`;

function buildConfig(profileId: string, fingerprint: Fingerprint) {
	return {
		...fingerprint,
		_profileHash: hashCode(profileId),
		_clientHints: deriveClientHints(fingerprint),
	};
}

function writePreload(name: string, template: string, config: object): string {
	const content = template.replace("__PANE_FP_CONFIG__", () =>
		JSON.stringify(config),
	);

	const tmpDir = path.join(app.getPath("temp"), "pane-fingerprints");

	if (!fs.existsSync(tmpDir)) {
		fs.mkdirSync(tmpDir, { recursive: true });
	}

	const filePath = path.join(tmpDir, name);
	fs.writeFileSync(filePath, content, "utf-8");

	return filePath;
}

export function generateFingerprintPreload(
	profileId: string,
	fingerprint: Fingerprint,
): string {
	return writePreload(
		`fp-${profileId}.js`,
		PRELOAD_TEMPLATE,
		buildConfig(profileId, fingerprint),
	);
}

export function generateWorkerFingerprintPreload(
	profileId: string,
	fingerprint: Fingerprint,
): string {
	return writePreload(
		`fp-worker-${profileId}.js`,
		WORKER_TEMPLATE,
		buildConfig(profileId, fingerprint),
	);
}

export function cleanupFingerprintPreload(profileId: string): void {
	for (const name of [`fp-${profileId}.js`, `fp-worker-${profileId}.js`]) {
		try {
			fs.unlinkSync(path.join(app.getPath("temp"), "pane-fingerprints", name));
		} catch {}
	}
}

function hashCode(str: string): number {
	let hash = 0;

	for (let i = 0; i < str.length; i++) {
		hash = (hash * 31 + str.charCodeAt(i)) | 0;
	}

	return Math.abs(hash);
}
