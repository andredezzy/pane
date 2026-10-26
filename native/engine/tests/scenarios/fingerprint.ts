import { cpus } from "node:os";
import { launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// Per-profile fingerprints inside the engine: profile A claims Windows, B
// Linux, and C has none. The fixture reports what a main frame, an iframe,
// and dedicated, shared, and service workers see, and the request headers of
// a fetch, a navigation, and a service worker's fetch. Each fingerprinted
// profile must report exactly its configured values everywhere, as native
// functions, with Client Hints headers that match navigator.userAgentData;
// C must report the Mac it runs on.

type Brand = { brand: string; version: string };

type Report = {
	userAgent: string;
	platform: string;
	hardwareConcurrency: number;
	deviceMemory: number;
	language: string;
	languages: string[];
	timezone: string;
	locale: string;
	numberLocale: string;
	offsets: number[];
	hours: number[];
	userAgentData: {
		brands: Brand[];
		mobile: boolean;
		platform: string;
		high: Record<string, unknown> & { fullVersionList: Brand[] };
	};
	webgl: { vendor: string; renderer: string } | null;
	canvas: {
		imageData: string;
		imageDataAgain: string;
		noisyChannels: number;
		regionMatches: boolean;
		blob: string;
		element?: string;
		dataURL?: string;
		dataURLAgain?: string;
		toBlob?: string;
	};
	native: { checked: number; failures: string[] };
	maxTouchPoints?: number;
	screen?: Record<string, number | boolean>;
	devicePixelRatio?: number;
	audio?: { sum: string; again: string } | null;
	voices?: number | null;
};

type Collected = {
	early: Record<string, unknown>;
	frame: Report;
	iframe: Report;
	dedicated: Report;
	shared: Report;
	service: Report;
	headers: Record<
		"fetch" | "navigation" | "serviceWorker",
		Record<string, string>
	>;
};

const CONTEXTS = ["frame", "iframe", "dedicated", "shared", "service"] as const;

const WINDOWS = {
	userAgent:
		"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36",
	platform: "WINDOWS",
	screen: { width: 1920, height: 1080, colorDepth: 24 },
	language: "en-US",
	languages: ["en-US", "en"],
	timezone: "America/New_York",
	webgl: {
		vendor: "Google Inc. (NVIDIA)",
		renderer:
			"ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0, D3D11)",
	},
	hardwareConcurrency: 8,
	deviceMemory: 8,
	maxTouchPoints: 0,
	canvas: { noise: true },
	audio: { noise: true },
};

const LINUX = {
	userAgent:
		"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36",
	platform: "LINUX",
	screen: { width: 1366, height: 768, colorDepth: 24 },
	language: "pt-BR",
	languages: ["pt-BR", "pt", "en"],
	timezone: "Asia/Tokyo",
	webgl: {
		vendor: "Google Inc. (Intel)",
		renderer: "ANGLE (Intel, Mesa Intel(R) UHD Graphics 630, OpenGL 4.6)",
	},
	hardwareConcurrency: 4,
	deviceMemory: 4,
	maxTouchPoints: 0,
	canvas: { noise: true },
	audio: { noise: true },
};

// What each claimed profile must report, beyond the fingerprint's own fields.
const CLAIMED = {
	a: {
		fingerprint: WINDOWS,
		navigatorPlatform: "Win32",
		uaPlatform: "Windows",
		platformVersion: "15.0.0",
		screen: {
			width: 1920,
			height: 1080,
			availWidth: 1920,
			availHeight: 1032,
			availLeft: 0,
			availTop: 0,
			colorDepth: 24,
			pixelDepth: 24,
			isExtended: false,
		},
		// 2026-01-15 and 2026-07-15 at 12:00 UTC.
		offsets: [300, 240],
		hours: [7, 8],
	},
	b: {
		fingerprint: LINUX,
		navigatorPlatform: "Linux x86_64",
		uaPlatform: "Linux",
		platformVersion: "",
		screen: {
			width: 1366,
			height: 768,
			availWidth: 1366,
			availHeight: 741,
			availLeft: 0,
			availTop: 0,
			colorDepth: 24,
			pixelDepth: 24,
			isExtended: false,
		},
		offsets: [-540, -540],
		hours: [21, 21],
	},
} as const;

const serialize = (brands: Brand[]) =>
	brands.map(({ brand, version }) => `"${brand}";v="${version}"`).join(", ");

const same = (a: unknown, b: unknown) =>
	JSON.stringify(a) === JSON.stringify(b);

const fingerprint: Scenario = async (context) => {
	const { app, trace } = await launchTraced(context, "fingerprint");

	const profiles = {
		a: await app.createProfile("Windows", "BLUE"),
		b: await app.createProfile("Linux", "EMERALD"),
		c: await app.createProfile("Real", "VIOLET"),
	};

	// Before any tab of theirs opens.
	await app.send("setProfileFingerprint", {
		profile: profiles.a,
		fingerprint: WINDOWS,
	});

	await app.send("setProfileFingerprint", {
		profile: profiles.b,
		fingerprint: LINUX,
	});

	const collected = {} as Record<"a" | "b" | "c", Collected>;

	for (const key of ["a", "b", "c"] as const) {
		const url = context.fixtures.url(`fingerprint.html?profile=${key}`);
		const tab = await app.openTab(profiles[key], url);
		const page = await app.page(tab, url);
		collected[key] = await page.evaluate<Collected>("collectAll()");
	}

	const real = collected.c.frame;
	const engineVersion = String(real.userAgentData.high.uaFullVersion);
	const engineMajor = engineVersion.split(".")[0];

	const realGrease = real.userAgentData.brands.find((entry) =>
		entry.brand.startsWith("Not"),
	);

	const checks: Record<string, unknown> = {};

	for (const key of ["a", "b"] as const) {
		const claimed = CLAIMED[key];
		const configured = claimed.fingerprint;
		const reports = collected[key];

		const userAgent = configured.userAgent.replace(
			/Chrome\/[\d.]+/,
			`Chrome/${engineMajor}.0.0.0`,
		);

		expect(
			real.userAgent.includes(`Chrome/${engineMajor}.0.0.0`),
			`the engine's User-Agent is not reduced: ${real.userAgent}`,
		);

		for (const name of CONTEXTS) {
			const report = reports[name];
			const label = `${key}.${name}`;

			const expected = {
				userAgent,
				platform: claimed.navigatorPlatform,
				hardwareConcurrency: configured.hardwareConcurrency,
				deviceMemory: configured.deviceMemory,
				language: configured.language,
				languages: configured.languages,
				timezone: configured.timezone,
				locale: configured.language,
				numberLocale: configured.language,
				offsets: claimed.offsets,
				hours: claimed.hours,
			};

			const actual = Object.fromEntries(
				Object.keys(expected).map((field) => [
					field,
					report[field as keyof Report],
				]),
			);

			expect(
				same(actual, expected),
				`${label} reported ${JSON.stringify(actual)}, not ${JSON.stringify(expected)}`,
			);

			const data = report.userAgentData;

			const expectedData = {
				platform: claimed.uaPlatform,
				mobile: false,
				platformVersion: claimed.platformVersion,
				architecture: "x86",
				bitness: "64",
				model: "",
				wow64: false,
				formFactors: ["Desktop"],
				uaFullVersion: engineVersion,
			};

			const actualData = {
				platform: data.platform,
				mobile: data.mobile,
				platformVersion: data.high.platformVersion,
				architecture: data.high.architecture,
				bitness: data.high.bitness,
				model: data.high.model,
				wow64: data.high.wow64,
				formFactors: data.high.formFactors,
				uaFullVersion: data.high.uaFullVersion,
			};

			expect(
				same(actualData, expectedData),
				`${label} userAgentData ${JSON.stringify(actualData)}`,
			);

			// Google Chrome's brands, with the engine's own GREASE brand.
			for (const brand of ["Chromium", "Google Chrome"]) {
				expect(
					data.brands.some((entry) =>
						same(entry, { brand, version: engineMajor }),
					) &&
						data.high.fullVersionList.some((entry) =>
							same(entry, { brand, version: engineVersion }),
						),
					`${label} brands ${JSON.stringify(data.brands)} lack ${brand}`,
				);
			}

			expect(
				data.brands.length === 3 &&
					realGrease !== undefined &&
					data.brands.some((entry) => same(entry, realGrease)),
				`${label} GREASE ${JSON.stringify(data.brands)} is not the engine's ${JSON.stringify(realGrease)}`,
			);

			if (report.webgl) {
				expect(
					same(report.webgl, configured.webgl),
					`${label} WebGL ${JSON.stringify(report.webgl)}`,
				);
			}

			expect(
				report.native.failures.length === 0,
				`${label} non-native: ${report.native.failures.join(", ")}`,
			);

			const canvas = report.canvas;

			expect(
				canvas.noisyChannels > 0 &&
					canvas.regionMatches &&
					canvas.imageData === canvas.imageDataAgain,
				`${label} canvas ${JSON.stringify(canvas)}`,
			);

			expect(
				canvas.imageData === reports.frame.canvas.imageData,
				`${label}'s canvas noise differs from its frame's`,
			);
		}

		const frame = reports.frame;

		expect(
			same(frame.webgl, configured.webgl),
			`${key} frame WebGL ${JSON.stringify(frame.webgl)}`,
		);

		expect(
			same(frame.screen, claimed.screen),
			`${key} screen ${JSON.stringify(frame.screen)}`,
		);

		expect(
			frame.devicePixelRatio === 1,
			`${key} devicePixelRatio ${frame.devicePixelRatio}`,
		);

		expect(
			frame.maxTouchPoints === configured.maxTouchPoints,
			`${key} maxTouchPoints ${frame.maxTouchPoints}`,
		);

		expect(frame.voices === 0, `${key} exposes ${frame.voices} host voices`);

		expect(
			frame.audio?.sum === frame.audio?.again &&
				frame.audio?.sum !== real.audio?.sum,
			`${key} audio ${JSON.stringify(frame.audio)} vs ${JSON.stringify(real.audio)}`,
		);

		expect(
			frame.canvas.element === frame.canvas.imageData &&
				frame.canvas.dataURL === frame.canvas.dataURLAgain &&
				frame.canvas.dataURL !== real.canvas.dataURL &&
				frame.canvas.toBlob !== real.canvas.toBlob,
			`${key} canvas exports ${JSON.stringify(frame.canvas)}`,
		);

		expect(
			same(reports.early, {
				userAgent,
				platform: claimed.navigatorPlatform,
				hardwareConcurrency: configured.hardwareConcurrency,
				timezone: configured.timezone,
				screenWidth: configured.screen.width,
			}),
			`${key}'s first script saw ${JSON.stringify(reports.early)}`,
		);

		// The headers each request carried match navigator.userAgentData.
		const data = frame.userAgentData;
		const headerChecks: Record<string, unknown> = {};

		for (const [source, headers] of Object.entries(reports.headers)) {
			const expectedHeaders: Record<string, string> = {
				"user-agent": userAgent,
				"sec-ch-ua": serialize(data.brands),
				"sec-ch-ua-mobile": "?0",
				"sec-ch-ua-platform": `"${data.platform}"`,
			};

			const highEntropy: Record<string, string> = {
				"sec-ch-ua-full-version-list": serialize(data.high.fullVersionList),
				"sec-ch-ua-platform-version": `"${data.high.platformVersion}"`,
				"sec-ch-ua-arch": `"${data.high.architecture}"`,
				"sec-ch-ua-bitness": `"${data.high.bitness}"`,
				"sec-ch-ua-model": `"${data.high.model}"`,
				"sec-ch-ua-wow64": "?0",
				"sec-ch-ua-form-factors": `"Desktop"`,
			};

			Object.assign(expectedHeaders, highEntropy);

			// A service worker's own fetch carries the hints Chrome sends from
			// service workers, the ones C's carries.
			if (source === "serviceWorker") {
				for (const name of Object.keys(expectedHeaders)) {
					if (
						name !== "user-agent" &&
						!(name in collected.c.headers.serviceWorker)
					) {
						delete expectedHeaders[name];
					}
				}
			}

			for (const [name, value] of Object.entries(expectedHeaders)) {
				expect(
					headers[name] === value,
					`${key} ${source} ${name}: ${headers[name]} ≠ ${value}`,
				);
			}

			expect(
				(headers["accept-language"] ?? "").startsWith(configured.language),
				`${key} ${source} Accept-Language ${headers["accept-language"]}`,
			);

			headerChecks[source] = Object.fromEntries(
				Object.entries(headers).filter(
					([name]) =>
						name.startsWith("sec-ch-ua") ||
						name === "user-agent" ||
						name === "accept-language",
				),
			);
		}

		checks[key] = {
			profile: profiles[key],
			configured,
			reported: Object.fromEntries(
				CONTEXTS.map((name) => [name, reports[name]]),
			),
			early: reports.early,
			headers: headerChecks,
		};
	}

	// A and B are told apart by their noise; C has none.
	expect(
		collected.a.frame.canvas.imageData !== collected.b.frame.canvas.imageData,
		"A and B share canvas noise",
	);

	expect(
		collected.a.frame.audio?.sum !== collected.b.frame.audio?.sum,
		"A and B share audio noise",
	);

	// C presents the Mac it runs on.
	for (const name of CONTEXTS) {
		const report = collected.c[name];

		expect(
			report.platform === "MacIntel" &&
				report.hardwareConcurrency === cpus().length &&
				report.userAgentData.platform === "macOS" &&
				report.canvas.noisyChannels === 0 &&
				report.native.failures.length === 0,
			`C ${name} reported ${JSON.stringify({ platform: report.platform, hardwareConcurrency: report.hardwareConcurrency, uaPlatform: report.userAgentData.platform, noisyChannels: report.canvas.noisyChannels })}`,
		);
	}

	expect(
		collected.c.headers.fetch["sec-ch-ua"] ===
			serialize(real.userAgentData.brands) &&
			collected.c.headers.fetch["user-agent"] === real.userAgent,
		`C's headers ${JSON.stringify(collected.c.headers.fetch)}`,
	);

	// Each fingerprinted profile's renderers got its fingerprint; C's none.
	const renderers = trace().filter(
		(entry) => entry.name === "Fingerprints.renderer",
	);

	const sets = trace().filter((entry) => entry.name === "Fingerprints.set");

	for (const key of ["a", "b"] as const) {
		expect(
			renderers.some((entry) => entry.args.identifier === profiles[key]),
			`no renderer of ${key} got its fingerprint`,
		);
	}

	expect(
		!renderers.some((entry) => entry.args.identifier === profiles.c),
		"a renderer of C got a fingerprint",
	);

	return {
		engine: { version: engineVersion, grease: realGrease },
		profiles: checks,
		real: {
			profile: profiles.c,
			userAgent: real.userAgent,
			platform: real.platform,
			brands: real.userAgentData.brands,
			noisyChannels: real.canvas.noisyChannels,
			native: real.native,
		},
		noise: {
			canvas: {
				a: collected.a.frame.canvas.imageData,
				b: collected.b.frame.canvas.imageData,
				c: real.canvas.imageData,
			},
			audio: {
				a: collected.a.frame.audio?.sum,
				b: collected.b.frame.audio?.sum,
				c: real.audio?.sum,
			},
		},
		trace: {
			set: sets.map((entry) => entry.args),
			renderers: renderers.map((entry) => entry.args),
		},
	};
};

export default fingerprint;
