// What a page or worker can read about the browser it runs in: the fingerprint
// scenario runs this in a frame, an iframe, and dedicated, shared, and service
// workers, and compares each against the profile's configured fingerprint.
// Loaded with <script src> in frames and importScripts() in workers.

const UNMASKED_VENDOR = 0x9245;
const UNMASKED_RENDERER = 0x9246;

const HIGH_ENTROPY = [
	"architecture",
	"bitness",
	"formFactors",
	"fullVersionList",
	"model",
	"platformVersion",
	"uaFullVersion",
	"wow64",
];

// FNV-1a over bytes, as hex.
function hashBytes(bytes) {
	let hash = 0x811c9dc5;

	for (const byte of bytes) {
		hash ^= byte;
		hash = Math.imul(hash, 0x01000193) >>> 0;
	}

	return hash.toString(16).padStart(8, "0");
}

function webGL() {
	try {
		const canvas =
			typeof document !== "undefined"
				? document.createElement("canvas")
				: new OffscreenCanvas(1, 1);

		const gl = canvas.getContext("webgl");

		if (!gl?.getExtension("WEBGL_debug_renderer_info")) {
			return null;
		}

		return {
			vendor: gl.getParameter(UNMASKED_VENDOR),
			renderer: gl.getParameter(UNMASKED_RENDERER),
		};
	} catch {
		return null;
	}
}

// Two opaque rectangles: every pixel is a known color, so any difference
// is the canvas noise.
function draw(context) {
	context.fillStyle = "rgb(200, 100, 50)";
	context.fillRect(0, 0, 64, 32);
	context.fillStyle = "rgb(10, 220, 120)";
	context.fillRect(16, 8, 32, 16);
}

function expectedPixel(x, y) {
	return x >= 16 && x < 48 && y >= 8 && y < 24
		? [10, 220, 120]
		: [200, 100, 50];
}

async function canvasProbe() {
	const offscreen = new OffscreenCanvas(64, 32);
	const context = offscreen.getContext("2d");
	draw(context);

	const full = context.getImageData(0, 0, 64, 32).data;
	const region = context.getImageData(16, 8, 8, 8).data;
	let noisyChannels = 0;

	for (let y = 0; y < 32; y++) {
		for (let x = 0; x < 64; x++) {
			const expected = expectedPixel(x, y);

			for (let channel = 0; channel < 3; channel++) {
				const delta = full[(y * 64 + x) * 4 + channel] - expected[channel];

				if (delta !== 0) {
					noisyChannels++;
				}
			}
		}
	}

	// A sub-region read gets the same noise as the full read.
	let regionMatches = true;

	for (let y = 0; y < 8; y++) {
		for (let x = 0; x < 8; x++) {
			for (let channel = 0; channel < 4; channel++) {
				if (
					region[(y * 8 + x) * 4 + channel] !==
					full[((y + 8) * 64 + x + 16) * 4 + channel]
				) {
					regionMatches = false;
				}
			}
		}
	}

	const blob = await offscreen.convertToBlob();

	const result = {
		imageData: hashBytes(full),
		imageDataAgain: hashBytes(context.getImageData(0, 0, 64, 32).data),
		noisyChannels,
		regionMatches,
		blob: hashBytes(new Uint8Array(await blob.arrayBuffer())),
	};

	if (typeof document !== "undefined") {
		const element = document.createElement("canvas");
		element.width = 64;
		element.height = 32;
		const elementContext = element.getContext("2d");
		draw(elementContext);
		result.element = hashBytes(elementContext.getImageData(0, 0, 64, 32).data);
		result.dataURL = hashBytes(new TextEncoder().encode(element.toDataURL()));

		result.dataURLAgain = hashBytes(
			new TextEncoder().encode(element.toDataURL()),
		);

		const elementBlob = await new Promise((done) => element.toBlob(done));
		result.toBlob = hashBytes(new Uint8Array(await elementBlob.arrayBuffer()));
	}

	return result;
}

// FingerprintJS's audio probe: an oscillator through a compressor.
async function audioProbe() {
	if (typeof OfflineAudioContext === "undefined") {
		return null;
	}

	const render = async () => {
		const context = new OfflineAudioContext(1, 5000, 44100);
		const oscillator = context.createOscillator();
		oscillator.type = "triangle";
		oscillator.frequency.value = 10000;
		const compressor = context.createDynamicsCompressor();
		oscillator.connect(compressor);
		compressor.connect(context.destination);
		oscillator.start(0);
		const buffer = await context.startRendering();
		const samples = buffer.getChannelData(0);
		let sum = 0;

		for (let index = 4500; index < 5000; index++) {
			sum += Math.abs(samples[index]);
		}

		return sum.toString();
	};

	return { sum: await render(), again: await render() };
}

// Whether each surface a fingerprint changes is still a native function.
function nativeChecks() {
	const isWindow = typeof window !== "undefined";
	const navigatorPrototype = Object.getPrototypeOf(navigator);

	const getter = (object, name) =>
		Object.getOwnPropertyDescriptor(object, name)?.get;

	const checks = {
		"navigator.platform": getter(navigatorPrototype, "platform"),
		"navigator.userAgent": getter(navigatorPrototype, "userAgent"),
		"navigator.language": getter(navigatorPrototype, "language"),
		"navigator.languages": getter(navigatorPrototype, "languages"),
		"navigator.hardwareConcurrency": getter(
			navigatorPrototype,
			"hardwareConcurrency",
		),
		"navigator.deviceMemory": getter(navigatorPrototype, "deviceMemory"),
		"NavigatorUAData.getHighEntropyValues":
			globalThis.NavigatorUAData?.prototype.getHighEntropyValues,
		"OffscreenCanvasRenderingContext2D.getImageData":
			OffscreenCanvasRenderingContext2D.prototype.getImageData,
		"OffscreenCanvas.convertToBlob": OffscreenCanvas.prototype.convertToBlob,
		"Date.getTimezoneOffset": Date.prototype.getTimezoneOffset,
		"Intl.DateTimeFormat": Intl.DateTimeFormat,
		"Function.toString": Function.prototype.toString,
	};

	if (isWindow) {
		Object.assign(checks, {
			"navigator.maxTouchPoints": getter(navigatorPrototype, "maxTouchPoints"),
			"screen.width": getter(Screen.prototype, "width"),
			"screen.availHeight": getter(Screen.prototype, "availHeight"),
			"screen.colorDepth": getter(Screen.prototype, "colorDepth"),
			"window.devicePixelRatio": getter(window, "devicePixelRatio"),
			"HTMLCanvasElement.toDataURL": HTMLCanvasElement.prototype.toDataURL,
			"HTMLCanvasElement.toBlob": HTMLCanvasElement.prototype.toBlob,
			"CanvasRenderingContext2D.getImageData":
				CanvasRenderingContext2D.prototype.getImageData,
			"WebGLRenderingContext.getParameter":
				WebGLRenderingContext.prototype.getParameter,
			"WebGL2RenderingContext.getParameter":
				WebGL2RenderingContext.prototype.getParameter,
			"OfflineAudioContext.startRendering":
				OfflineAudioContext.prototype.startRendering,
			"SpeechSynthesis.getVoices": SpeechSynthesis.prototype.getVoices,
		});
	}

	const failures = Object.entries(checks)
		.filter(
			([, fn]) =>
				typeof fn !== "function" ||
				!/\{\s*\[native code\]\s*\}$/.test(
					Function.prototype.toString.call(fn),
				),
		)
		.map(([name]) => name);

	return { checked: Object.keys(checks).length, failures };
}

async function voices() {
	if (typeof speechSynthesis === "undefined") {
		return null;
	}

	// The host's voices load asynchronously.
	await new Promise((done) => setTimeout(done, 1500));

	return speechSynthesis.getVoices().length;
}

self.collectFingerprint = async function collectFingerprint() {
	const data = navigator.userAgentData;
	const january = new Date(Date.UTC(2026, 0, 15, 12));
	const july = new Date(Date.UTC(2026, 6, 15, 12));

	const result = {
		userAgent: navigator.userAgent,
		platform: navigator.platform,
		hardwareConcurrency: navigator.hardwareConcurrency,
		deviceMemory: navigator.deviceMemory,
		language: navigator.language,
		languages: [...navigator.languages],
		timezone: Intl.DateTimeFormat().resolvedOptions().timeZone,
		locale: Intl.DateTimeFormat().resolvedOptions().locale,
		numberLocale: new Intl.NumberFormat().resolvedOptions().locale,
		offsets: [january.getTimezoneOffset(), july.getTimezoneOffset()],
		hours: [january.getHours(), july.getHours()],
		userAgentData: data
			? {
					brands: data.brands.map(({ brand, version }) => ({ brand, version })),
					mobile: data.mobile,
					platform: data.platform,
					high: await data.getHighEntropyValues(HIGH_ENTROPY),
				}
			: null,
		webgl: webGL(),
		canvas: await canvasProbe(),
		native: nativeChecks(),
	};

	if (typeof window !== "undefined") {
		Object.assign(result, {
			maxTouchPoints: navigator.maxTouchPoints,
			screen: {
				width: screen.width,
				height: screen.height,
				availWidth: screen.availWidth,
				availHeight: screen.availHeight,
				availLeft: screen.availLeft,
				availTop: screen.availTop,
				colorDepth: screen.colorDepth,
				pixelDepth: screen.pixelDepth,
				isExtended: screen.isExtended,
			},
			devicePixelRatio: window.devicePixelRatio,
			audio: await audioProbe(),
			voices: await voices(),
		});
	}

	return result;
};
