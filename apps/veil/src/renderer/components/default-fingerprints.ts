import type { Fingerprint } from "../../stores/profile-store";

export const DEFAULT_FINGERPRINTS: Record<string, Fingerprint> = {
	windows: {
		userAgent:
			"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36",
		platform: "windows",
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
		deviceMemory: 16,
		maxTouchPoints: 0,
		canvas: { noise: true },
		audio: { noise: true },
	},
	macos: {
		userAgent:
			"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36",
		platform: "macos",
		screen: { width: 1440, height: 900, colorDepth: 30 },
		language: "en-US",
		languages: ["en-US", "en"],
		timezone: "America/New_York",
		webgl: {
			vendor: "Google Inc. (Apple)",
			renderer: "ANGLE (Apple, Apple M1, OpenGL 4.1)",
		},
		hardwareConcurrency: 8,
		deviceMemory: 8,
		maxTouchPoints: 0,
		canvas: { noise: true },
		audio: { noise: true },
	},
	linux: {
		userAgent:
			"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36",
		platform: "linux",
		screen: { width: 1920, height: 1080, colorDepth: 24 },
		language: "en-US",
		languages: ["en-US", "en"],
		timezone: "America/New_York",
		webgl: {
			vendor: "Google Inc. (Intel)",
			renderer: "ANGLE (Intel, Mesa Intel(R) UHD Graphics 630, OpenGL 4.6)",
		},
		hardwareConcurrency: 4,
		deviceMemory: 8,
		maxTouchPoints: 0,
		canvas: { noise: true },
		audio: { noise: true },
	},
};
