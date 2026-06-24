import fs from "node:fs";
import path from "node:path";
import { app } from "electron";
import { type Fingerprint, Platform } from "../../stores/profile-store";
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

	// Define a spoofed prototype attribute. The original getter is called first for
	// its native brand check, so accessing the property on a wrong receiver still
	// throws "Illegal invocation" exactly like native (creepjs probes this).
	const defineGetter = (proto, prop, value) => {
		const original = Object.getOwnPropertyDescriptor(proto, prop);
		const originalGet = original && original.get;
		const descriptor = {
			get: asNative(function () {
				if (originalGet) originalGet.call(this);
				return value;
			}, "get " + prop),
			configurable: true,
			enumerable: original ? original.enumerable : false,
		};
		// Preserve a [Replaceable] setter (e.g. window.devicePixelRatio) so the
		// descriptor still has a setter and "x.prop = v" keeps native semantics.
		if (original && original.set) descriptor.set = original.set;
		Object.defineProperty(proto, prop, descriptor);
	};

	// Replace a native method, preserving its descriptor flags AND its arity (.length)
	// so a wrapper with a different parameter count can't be spotted; makeWrapper gets
	// the original so it can delegate the non-spoofed path.
	const patchMethod = (obj, prop, makeWrapper) => {
		const descriptor = Object.getOwnPropertyDescriptor(obj, prop);
		if (!descriptor || typeof descriptor.value !== "function") return;
		const wrapper = asNative(makeWrapper(descriptor.value), prop);
		Object.defineProperty(wrapper, "length", { value: descriptor.value.length, configurable: true });
		Object.defineProperty(obj, prop, Object.assign({}, descriptor, { value: wrapper }));
	};
	// Replace a native method outright (no delegation to the original).
	const replaceMethod = (obj, prop, fn) => patchMethod(obj, prop, () => fn);

	const navProto = Object.getPrototypeOf(navigator);
	defineGetter(navProto, "platform", fp._navPlatform);
	defineGetter(navProto, "hardwareConcurrency", fp.hardwareConcurrency);
	defineGetter(navProto, "deviceMemory", fp.deviceMemory);
	// maxTouchPoints is a Navigator property only — a real WorkerNavigator lacks it.
	if ("maxTouchPoints" in navigator) {
		defineGetter(navProto, "maxTouchPoints", fp.maxTouchPoints);
	}
	defineGetter(navProto, "language", fp.language);
	defineGetter(navProto, "languages", Object.freeze(fp.languages.slice()));

	// navigator.plugins / mimeTypes / pdfViewerEnabled are intentionally NOT spoofed:
	// a JS-built PluginArray (Object.create(PluginArray.prototype)) can't reproduce
	// the native named-getter / internal-slot / plugins<->mimeTypes linkage, so a
	// mock is more detectable than the real (native or empty) collection. Left to
	// Chromium's own PDF-viewer support.

	// speechSynthesis.getVoices() exposes the host OS's TTS voices (Apple vs
	// Microsoft), contradicting the platform spoof. Report none (as if not yet
	// loaded) rather than the host set.
	if (typeof SpeechSynthesis !== "undefined" && typeof speechSynthesis !== "undefined") {
		replaceMethod(SpeechSynthesis.prototype, "getVoices", function getVoices() { return []; });
	}

	// navigator.storage.estimate().quota can leak the host disk size for origins that
	// hold the unlimited-storage permission. Chrome 133+ otherwise reports quota =
	// usage + 10 GiB for ordinary origins (a predictable-quota formula, deliberately
	// identical in regular and incognito mode — not a static value). Mirror that
	// exactly: forward the real usage/usageDetails so a write-then-estimate probe
	// still sees usage move, and pin quota to usage + 10 GiB so the value matches
	// real Chrome and never exposes the disk.
	if (typeof StorageManager !== "undefined") {
		patchMethod(StorageManager.prototype, "estimate", (original) => function estimate() {
			return original.call(this).then((real) =>
				Object.assign({}, real, { quota: (real.usage || 0) + 10 * 1024 * 1024 * 1024 }),
			);
		});
	}

	if (fp.screen && typeof screen !== "undefined") {
		// availWidth/availHeight/availTop leave room for the OS chrome; reporting the
		// full screen (or the host's menu-bar inset) is a tell.
		const inset = { WINDOWS: 48, MACOS: 25, LINUX: 27 }[fp.platform] || 0;
		const availTop = fp.platform === "MACOS" ? 25 : 0;
		const screenProto = Object.getPrototypeOf(screen);
		defineGetter(screenProto, "width", fp.screen.width);
		defineGetter(screenProto, "height", fp.screen.height);
		defineGetter(screenProto, "availWidth", fp.screen.width);
		defineGetter(screenProto, "availHeight", fp.screen.height - inset);
		if ("availTop" in screen) defineGetter(screenProto, "availTop", availTop);
		if ("availLeft" in screen) defineGetter(screenProto, "availLeft", 0);
		if (fp.screen.colorDepth) {
			defineGetter(screenProto, "colorDepth", fp.screen.colorDepth);
			defineGetter(screenProto, "pixelDepth", fp.screen.colorDepth);
		}
	}

	// devicePixelRatio: a Retina host reports 2 while a spoofed 1080p Windows display
	// is almost always 1. Derive from the claimed platform.
	if (typeof window !== "undefined") {
		const dpr = fp.platform === "MACOS" ? 2 : 1;
		defineGetter(Object.getPrototypeOf(window), "devicePixelRatio", dpr);
	}

	// chrome.loadTimes() and chrome.csi() are deprecated but still injected into
	// every page by Chrome's renderer (loadtimes_extension_bindings.cc). Electron's
	// renderer client omits them, so their absence on window.chrome is a well-known
	// "not real Chrome" tell. Provide native-looking stubs backed by real timing.
	if (typeof window !== "undefined" && typeof performance !== "undefined") {
		let chromeGlobal = window.chrome;
		if (!chromeGlobal) {
			try {
				window.chrome = {};
			} catch (_error) {}
			chromeGlobal = window.chrome;
		}

		if (chromeGlobal && typeof chromeGlobal.csi !== "function") {
			chromeGlobal.csi = asNative(function csi() {
				const timing = performance.timing;
				return {
					onloadT: timing.domContentLoadedEventEnd,
					startE: timing.navigationStart,
					pageT: Date.now() - timing.navigationStart,
					tran: 15,
				};
			}, "csi");
		}

		if (chromeGlobal && typeof chromeGlobal.loadTimes !== "function") {
			const navigationTypeName = { reload: "Reload", back_forward: "BackForward" };
			chromeGlobal.loadTimes = asNative(function loadTimes() {
				const timing = performance.timing;
				const navEntry = performance.getEntriesByType("navigation")[0] || {};
				const paintEntry = performance.getEntriesByType("paint").find((entry) => entry.name === "first-paint");
				const protocol = navEntry.nextHopProtocol || "h2";
				const negotiated = protocol === "h2" || protocol === "hq";
				return {
					requestTime: timing.navigationStart / 1000,
					startLoadTime: timing.navigationStart / 1000,
					commitLoadTime: timing.responseStart / 1000,
					finishDocumentLoadTime: timing.domContentLoadedEventEnd / 1000,
					finishLoadTime: timing.loadEventEnd / 1000,
					firstPaintTime: paintEntry ? (timing.navigationStart + paintEntry.startTime) / 1000 : timing.loadEventEnd / 1000,
					firstPaintAfterLoadTime: 0,
					navigationType: navigationTypeName[navEntry.type] || "Other",
					wasFetchedViaSpdy: negotiated,
					wasNpnNegotiated: negotiated,
					npnNegotiatedProtocol: negotiated ? protocol : "unknown",
					wasAlternateProtocolAvailable: false,
					connectionInfo: protocol,
				};
			}, "loadTimes");
		}
	}

	// navigator.userAgentData — consistent with the Sec-CH-UA-* header rewrite (both
	// from client-hints.ts). Override the real NavigatorUAData prototype when present
	// so navigator.userAgentData stays a genuine instance; else synthesise one.
	const highEntropy = {
		architecture: ch.architecture,
		bitness: ch.bitness,
		brands: ch.brands,
		formFactors: ["Desktop"],
		fullVersionList: ch.fullVersionList,
		mobile: ch.mobile,
		model: "",
		platform: ch.platform,
		platformVersion: ch.platformVersion,
		uaFullVersion: ch.uaFullVersion,
		wow64: false,
	};
	function getHighEntropyValues(hints) {
		const result = { brands: ch.brands.slice(), mobile: ch.mobile, platform: ch.platform };
		if (Array.isArray(hints)) {
			for (const hint of hints) {
				if (hint in highEntropy) {
					const value = highEntropy[hint];
					result[hint] = Array.isArray(value) ? value.slice() : value;
				}
			}
		}
		return Promise.resolve(result);
	}
	function toJSON() {
		return { brands: ch.brands.slice(), mobile: ch.mobile, platform: ch.platform };
	}

	if (typeof NavigatorUAData !== "undefined" && navigator.userAgentData) {
		const uaProto = NavigatorUAData.prototype;
		defineGetter(uaProto, "brands", ch.brands);
		defineGetter(uaProto, "mobile", ch.mobile);
		defineGetter(uaProto, "platform", ch.platform);
		replaceMethod(uaProto, "getHighEntropyValues", getHighEntropyValues);
		replaceMethod(uaProto, "toJSON", toJSON);
	} else {
		const base = typeof NavigatorUAData !== "undefined" ? NavigatorUAData.prototype : Object.prototype;
		const uaProto = Object.create(base);
		defineGetter(uaProto, "brands", ch.brands);
		defineGetter(uaProto, "mobile", ch.mobile);
		defineGetter(uaProto, "platform", ch.platform);
		Object.defineProperty(uaProto, "getHighEntropyValues", {
			value: asNative(getHighEntropyValues, "getHighEntropyValues"),
			writable: true, enumerable: false, configurable: true,
		});
		Object.defineProperty(uaProto, "toJSON", {
			value: asNative(toJSON, "toJSON"),
			writable: true, enumerable: false, configurable: true,
		});

		// Cache the instance so navigator.userAgentData has a stable identity.
		const uaInstance = Object.create(uaProto);
		defineGetter(navProto, "userAgentData", uaInstance);
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
	// disagree and lie-detectors (creepjs) flag the canvas. Only fully opaque pixels
	// are touched so the toDataURL putImageData round-trip (which premultiplies
	// alpha) can't diverge from the in-memory getImageData path.
	if (fp.canvas && fp.canvas.noise) {
		const seed = (fp._profileHash || 0) >>> 0;
		const deltaAt = (index) => {
			let h = (seed ^ index) >>> 0;
			h = Math.imul(h ^ (h >>> 16), 0x45d9f3b3);
			h = Math.imul(h ^ (h >>> 16), 0x45d9f3b3);
			h = (h ^ (h >>> 16)) >>> 0;
			return h / 4294967296 < 0.5 ? -1 : 1;
		};
		const noiseRegion = (imageData, originX, originY, fullWidth) => {
			const data = imageData.data;
			for (let row = 0; row < imageData.height; row++) {
				for (let col = 0; col < imageData.width; col++) {
					const di = (row * imageData.width + col) * 4;
					if (data[di + 3] !== 255) continue;
					const absolute = ((originY + row) * fullWidth + (originX + col)) * 3;
					data[di] = data[di] + deltaAt(absolute);
					data[di + 1] = data[di + 1] + deltaAt(absolute + 1);
					data[di + 2] = data[di + 2] + deltaAt(absolute + 2);
				}
			}
		};

		// A service worker exposes OffscreenCanvas* but not the window-only
		// CanvasRenderingContext2D, so resolve whichever prototypes this realm has and
		// noise each independently — the OffscreenCanvas path must still run in the SW.
		const ctx2dProto = typeof CanvasRenderingContext2D !== "undefined" ? CanvasRenderingContext2D.prototype : null;
		const offscreenProto = typeof OffscreenCanvasRenderingContext2D !== "undefined" ? OffscreenCanvasRenderingContext2D.prototype : null;
		const rawCtxGet = ctx2dProto ? ctx2dProto.getImageData : null;
		const rawOffscreenGet = offscreenProto ? offscreenProto.getImageData : null;
		const rawReaderFor = (context) =>
			offscreenProto && context instanceof OffscreenCanvasRenderingContext2D
				? rawOffscreenGet
				: rawCtxGet;

		const applyNoise = (canvas) => {
			const context = canvas.getContext && canvas.getContext("2d");
			if (!context || !canvas.width || !canvas.height) return null;
			const read = rawReaderFor(context);
			if (!read) return null;
			const original = read.call(context, 0, 0, canvas.width, canvas.height);
			const noised = read.call(context, 0, 0, canvas.width, canvas.height);
			noiseRegion(noised, 0, 0, canvas.width);
			context.putImageData(noised, 0, 0);
			return () => context.putImageData(original, 0, 0);
		};

		// Direct pixel reads are noised in place (using a raw reader to avoid double
		// application) so they match what toDataURL/toBlob encode.
		const makeGetImageData = (original) => function getImageData(sx, sy, sw, sh, settings) {
			const imageData = original.call(this, sx, sy, sw, sh, settings);
			noiseRegion(imageData, sx, sy, this.canvas.width);
			return imageData;
		};
		if (ctx2dProto) {
			patchMethod(ctx2dProto, "getImageData", makeGetImageData);
		}
		if (offscreenProto) {
			patchMethod(offscreenProto, "getImageData", makeGetImageData);
		}

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

	// Timezone: report fp.timezone from Intl, Temporal, AND every host-timezone-
	// dependent Date method (string, offset, and local numeric getters), which read
	// ICU directly and bypass the Intl.DateTimeFormat constructor.
	if (fp.timezone && typeof Intl !== "undefined") {
		const OriginalDTF = Intl.DateTimeFormat;
		const partsFormat = new OriginalDTF("en-US", {
			timeZone: fp.timezone, hourCycle: "h23",
			weekday: "short", year: "numeric", month: "short", day: "2-digit",
			hour: "2-digit", minute: "2-digit", second: "2-digit",
		});
		const numericFormat = new OriginalDTF("en-US", {
			timeZone: fp.timezone, hourCycle: "h23",
			weekday: "short", year: "numeric", month: "numeric", day: "numeric",
			hour: "numeric", minute: "numeric", second: "numeric",
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
		const padYear = (year) => (year.length < 4 ? year.padStart(4, "0") : year);
		const WEEKDAY = { Sun: 0, Mon: 1, Tue: 2, Wed: 3, Thu: 4, Fri: 5, Sat: 6 };
		const COMPONENT_KEYS = ["weekday", "era", "year", "month", "day", "hour", "minute", "second", "dayPeriod", "dateStyle", "timeStyle", "fractionalSecondDigits"];

		// Default the locale to the spoofed language too, so resolvedOptions().locale
		// agrees with navigator.language instead of leaking the host locale.
		const dtfProxy = new Proxy(OriginalDTF, {
			construct(target, args) {
				const locales = args[0] === undefined ? fp.language : args[0];
				const options = Object.assign({}, args[1]);
				if (!options.timeZone) options.timeZone = fp.timezone;
				return Reflect.construct(target, [locales, options]);
			},
			apply(target, thisArg, args) {
				const locales = args[0] === undefined ? fp.language : args[0];
				const options = Object.assign({}, args[1]);
				if (!options.timeZone) options.timeZone = fp.timezone;
				return Reflect.apply(target, thisArg, [locales, options]);
			},
		});
		OriginalDTF.prototype.constructor = dtfProxy;
		labels.set(dtfProxy, "DateTimeFormat");
		Intl.DateTimeFormat = dtfProxy;

		// TC39 Temporal (stable in recent Chromium) reads the host zone from ICU too.
		// timeZoneId() is one entry point; plainDateTimeISO/plainDateISO/plainTimeISO/
		// zonedDateTimeISO each hit ICU's default zone directly (they do NOT route
		// through timeZoneId), so a no-arg call leaks the host zone and contradicts
		// the spoof. Inject fp.timezone as the default zone, preserving any explicit arg.
		if (typeof Temporal !== "undefined" && Temporal.Now) {
			if (typeof Temporal.Now.timeZoneId === "function") {
				replaceMethod(Temporal.Now, "timeZoneId", function timeZoneId() { return fp.timezone; });
			}
			for (const method of ["plainDateTimeISO", "plainDateISO", "plainTimeISO", "zonedDateTimeISO"]) {
				if (typeof Temporal.Now[method] === "function") {
					patchMethod(Temporal.Now, method, (original) => function (timeZone) {
						return original.call(this, timeZone !== undefined ? timeZone : fp.timezone);
					});
				}
			}
		}

		replaceMethod(Date.prototype, "getTimezoneOffset", function getTimezoneOffset() {
			if (Number.isNaN(this.getTime())) return Number.NaN;
			const match = gmtOf(this).match(/GMT([+-])(\\d{2})(\\d{2})/);
			if (!match) return 0;
			return ((match[1] === "-" ? 1 : -1) * (parseInt(match[2], 10) * 60 + parseInt(match[3], 10))) || 0;
		});
		replaceMethod(Date.prototype, "toString", function toString() {
			if (Number.isNaN(this.getTime())) return "Invalid Date";
			const p = partsOf(this, partsFormat);
			return p.weekday + " " + p.month + " " + p.day + " " + padYear(p.year) + " " + p.hour + ":" + p.minute + ":" + p.second + " " + gmtOf(this) + " (" + nameOf(this) + ")";
		});
		replaceMethod(Date.prototype, "toDateString", function toDateString() {
			if (Number.isNaN(this.getTime())) return "Invalid Date";
			const p = partsOf(this, partsFormat);
			return p.weekday + " " + p.month + " " + p.day + " " + padYear(p.year);
		});
		replaceMethod(Date.prototype, "toTimeString", function toTimeString() {
			if (Number.isNaN(this.getTime())) return "Invalid Date";
			const p = partsOf(this, partsFormat);
			return p.hour + ":" + p.minute + ":" + p.second + " " + gmtOf(this) + " (" + nameOf(this) + ")";
		});

		// Local numeric getters in the spoofed zone (else getHours()-getUTCHours()
		// recovers the real host offset despite the patched getTimezoneOffset).
		const numeric = (date, part) => parseInt(partsOf(date, numericFormat)[part], 10);
		replaceMethod(Date.prototype, "getFullYear", function getFullYear() { return Number.isNaN(this.getTime()) ? Number.NaN : numeric(this, "year"); });
		replaceMethod(Date.prototype, "getMonth", function getMonth() { return Number.isNaN(this.getTime()) ? Number.NaN : numeric(this, "month") - 1; });
		replaceMethod(Date.prototype, "getDate", function getDate() { return Number.isNaN(this.getTime()) ? Number.NaN : numeric(this, "day"); });
		replaceMethod(Date.prototype, "getDay", function getDay() { return Number.isNaN(this.getTime()) ? Number.NaN : (WEEKDAY[partsOf(this, numericFormat).weekday] ?? 0); });
		replaceMethod(Date.prototype, "getHours", function getHours() { return Number.isNaN(this.getTime()) ? Number.NaN : numeric(this, "hour") % 24; });
		replaceMethod(Date.prototype, "getMinutes", function getMinutes() { return Number.isNaN(this.getTime()) ? Number.NaN : numeric(this, "minute"); });
		replaceMethod(Date.prototype, "getSeconds", function getSeconds() { return Number.isNaN(this.getTime()) ? Number.NaN : numeric(this, "second"); });
		replaceMethod(Date.prototype, "getYear", function getYear() { return Number.isNaN(this.getTime()) ? Number.NaN : numeric(this, "year") - 1900; });

		const patchLocale = (method, defaults) => replaceMethod(Date.prototype, method, function () {
			const locales = arguments[0] === undefined ? fp.language : arguments[0];
			const options = arguments[1];
			if (Number.isNaN(this.getTime())) return "Invalid Date";
			const opts = Object.assign({}, options);
			if (!COMPONENT_KEYS.some((key) => key in opts)) Object.assign(opts, defaults);
			if (!opts.timeZone) opts.timeZone = fp.timezone;
			return new OriginalDTF(locales, opts).format(this);
		});
		patchLocale("toLocaleString", { year: "numeric", month: "numeric", day: "numeric", hour: "numeric", minute: "numeric", second: "numeric" });
		patchLocale("toLocaleDateString", { year: "numeric", month: "numeric", day: "numeric" });
		patchLocale("toLocaleTimeString", { hour: "numeric", minute: "numeric", second: "numeric" });
	}

	// The other locale-sensitive Intl constructors leak the host locale via
	// resolvedOptions().locale; default their locale to the spoofed language too.
	if (typeof Intl !== "undefined" && fp.language) {
		const localeProxy = (Original, label) => {
			const proxy = new Proxy(Original, {
				construct(target, args) {
					const locales = args[0] === undefined ? fp.language : args[0];
					return Reflect.construct(target, [locales, ...args.slice(1)]);
				},
				apply(target, thisArg, args) {
					const locales = args[0] === undefined ? fp.language : args[0];
					return Reflect.apply(target, thisArg, [locales, ...args.slice(1)]);
				},
			});
			Original.prototype.constructor = proxy;
			labels.set(proxy, label);
			return proxy;
		};
		for (const name of ["NumberFormat", "Collator", "PluralRules", "RelativeTimeFormat", "ListFormat", "Segmenter", "DisplayNames", "DurationFormat"]) {
			if (typeof Intl[name] === "function") Intl[name] = localeProxy(Intl[name], name);
		}
	}

	// Dedicated workers spawned from page JS bypass the preload (Electron has no
	// dedicated-worker preload type). Wrap classic same-origin/blob workers so they
	// inherit the spoof; fall back to the native Worker for module/cross-origin (and
	// on any error) so this can never break a page.
	if (typeof Worker !== "undefined" && typeof Blob !== "undefined" && typeof URL !== "undefined" && typeof location !== "undefined") {
		// Pass the config as the IIFE argument (no self.__PANE_FP__ left lying around
		// in the worker scope).
		const spoofSource = "(" + __paneApplyFingerprint.toString() + ")(" + JSON.stringify(fp) + ");";
		const WorkerProxy = new Proxy(Worker, {
			construct(target, args) {
				try {
					const url = new URL(String(args[0]), location.href);
					const classic = !(args[1] && args[1].type === "module");
					const reachable = url.protocol === "blob:" || url.origin === location.origin;
					if (!classic || !reachable) return Reflect.construct(target, args);
					const wrapped = spoofSource + "importScripts(" + JSON.stringify(url.href) + ");";
					const blobUrl = URL.createObjectURL(new Blob([wrapped], { type: "text/javascript" }));
					try {
						return Reflect.construct(target, [blobUrl, args[1]]);
					} finally {
						URL.revokeObjectURL(blobUrl);
					}
				} catch (_error) {
					return Reflect.construct(target, args);
				}
			},
		});
		labels.set(WorkerProxy, "Worker");
		try {
			Object.defineProperty(globalThis, "Worker", { value: WorkerProxy, writable: true, configurable: true });
			Object.defineProperty(Worker.prototype, "constructor", { value: WorkerProxy, writable: true, configurable: true });
		} catch (error) {
			console.warn("[fp] Worker spoof install failed:", error);
		}
	}
}

let __paneBridge;
try {
	__paneBridge = require("electron").contextBridge;
} catch (_error) {
	__paneBridge = undefined;
}

if (__paneBridge && typeof __paneBridge.executeInMainWorld === "function") {
	try {
		__paneBridge.executeInMainWorld({ func: __paneApplyFingerprint, args: [__PANE_FP__] });
	} catch (_error) {
		__paneApplyFingerprint(__PANE_FP__);
	}
} else {
	// No bridge (older Electron / certain worker contexts): run in this scope.
	__paneApplyFingerprint(__PANE_FP__);
}
`;

const NAV_PLATFORM: Record<Platform, string> = {
	[Platform.WINDOWS]: "Win32",
	[Platform.MACOS]: "MacIntel",
	[Platform.LINUX]: "Linux x86_64",
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

	for (let index = 0; index < str.length; index++) {
		hash = (hash * 31 + str.charCodeAt(index)) | 0;
	}

	return Math.abs(hash);
}
