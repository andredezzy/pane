import { type Fingerprint, Platform } from "../../stores/profile-store";

export interface UABrand {
	brand: string;
	version: string;
}

export interface ClientHints {
	platform: string;
	platformVersion: string;
	architecture: string;
	bitness: string;
	mobile: boolean;
	brands: UABrand[];
	fullVersionList: UABrand[];
	uaFullVersion: string;
}

const UA_PLATFORM: Record<Platform, string> = {
	[Platform.WINDOWS]: "Windows",
	[Platform.MACOS]: "macOS",
	[Platform.LINUX]: "Linux",
};

// Real Chrome reports an empty Sec-CH-UA-Platform-Version on Linux
// (user_agent_utils.cc returns std::string() for IS_LINUX); any non-empty value
// paired with platform "Linux" is a tell. Windows/macOS report the OS version.
const PLATFORM_VERSION: Record<Platform, string> = {
	[Platform.WINDOWS]: "15.0.0",
	[Platform.MACOS]: "14.6.1",
	[Platform.LINUX]: "",
};

// Chromium's GREASE algorithm (user_agent_utils.cc): the fake brand is built from
// a fixed char table indexed by the major version, and its version is one of three
// constants. Reproducing it keeps the grease entry plausible for ANY claimed Chrome
// version instead of only the one we happened to hardcode.
const GREASE_CHARS = [" ", "(", ":", "-", ".", "/", ")", ";", "=", "?", "_"];
const GREASE_VERSIONS = ["8", "99", "24"];

function greasedBrand(major: number, versionIndex: number): UABrand {
	const brand = `Not${GREASE_CHARS[major % GREASE_CHARS.length]}A${
		GREASE_CHARS[(major + 1) % GREASE_CHARS.length]
	}Brand`;

	return { brand, version: GREASE_VERSIONS[versionIndex] };
}

function isAppleSilicon(fingerprint: Fingerprint): boolean {
	return /apple|\bm[1-4]\b|arm/i.test(fingerprint.webgl?.renderer ?? "");
}

// Rewrite the Chrome version in a claimed User-Agent to the real engine's, so a
// stored fingerprint can never advertise a Chrome build the underlying Chromium
// isn't. A stale claim (e.g. "Chrome/136" on a Chromium 146 engine) is a
// contradiction Cloudflare Turnstile detects by feature-probing the runtime, which
// is enough for it to withhold a clearance token and loop the challenge. Derived
// from the live UA at session setup, so it stays correct across every Electron bump.
export function reconcileChromeVersion(
	claimedUA: string,
	realUA: string,
): string {
	const realVersion = realUA.match(/Chrome\/(\d+\.\d+\.\d+\.\d+)/)?.[1];

	if (!realVersion) {
		return claimedUA;
	}

	// Replace ANY Chrome version form in the claim (e.g. "136.0.0.0" or a bare
	// "136"), not only 4-segment, so a non-standard stored fingerprint can't keep a
	// stale version.
	return claimedUA.replace(/(Chrome\/)[\d.]+/, `$1${realVersion}`);
}

// Single source of truth for a fingerprint's UA Client Hints, consumed by BOTH the
// main-world navigator.userAgentData spoof (fingerprint-preload) and the Sec-CH-UA-*
// request-header rewrite (profile). Deriving them once keeps the JS surface and the
// HTTP surface from drifting apart — a drift would re-introduce the very platform
// contradiction this spoof exists to remove.
export function deriveClientHints(fingerprint: Fingerprint): ClientHints {
	const fullMatch = fingerprint.userAgent.match(/Chrome\/(\d+\.\d+\.\d+\.\d+)/);
	const majorMatch = fingerprint.userAgent.match(/Chrome\/(\d+)/);
	const chromeMajor = majorMatch?.[1] ?? "136";
	const major = Number(chromeMajor);
	const uaFullVersion = fullMatch?.[1] ?? `${chromeMajor}.0.0.0`;

	// Chromium picks the grease VERSION by major % 3 but seeds its POSITION in the
	// list from (floor(major/10) + major) % 3 — they diverge for Chrome 130+.
	const greaseIndex = (Math.floor(major / 10) + major) % 3;
	const grease = greasedBrand(major, major % 3);

	const brands: UABrand[] = [
		{ brand: "Chromium", version: chromeMajor },
		{ brand: "Google Chrome", version: chromeMajor },
	];

	brands.splice(greaseIndex, 0, {
		brand: grease.brand,
		version: grease.version,
	});

	const fullVersionList: UABrand[] = [
		{ brand: "Chromium", version: uaFullVersion },
		{ brand: "Google Chrome", version: uaFullVersion },
	];

	fullVersionList.splice(greaseIndex, 0, {
		brand: grease.brand,
		version: `${grease.version}.0.0.0`,
	});

	return {
		platform: UA_PLATFORM[fingerprint.platform],
		platformVersion: PLATFORM_VERSION[fingerprint.platform],
		architecture: isAppleSilicon(fingerprint) ? "arm" : "x86",
		bitness: "64",
		mobile: false,
		brands,
		fullVersionList,
		uaFullVersion,
	};
}

// The Sec-CH-UA-* header values (lowercase keys) a fingerprinted profile should
// present. Applied only to headers Chromium already emits — never added — so a
// profile can't leak a client hint the browser had suppressed.
export function clientHintHeaders(hints: ClientHints): Record<string, string> {
	const serializeBrands = (list: UABrand[]) =>
		list.map((entry) => `"${entry.brand}";v="${entry.version}"`).join(", ");

	return {
		"sec-ch-ua": serializeBrands(hints.brands),
		"sec-ch-ua-full-version-list": serializeBrands(hints.fullVersionList),
		"sec-ch-ua-full-version": `"${hints.uaFullVersion}"`,
		"sec-ch-ua-platform": `"${hints.platform}"`,
		"sec-ch-ua-platform-version": `"${hints.platformVersion}"`,
		"sec-ch-ua-arch": `"${hints.architecture}"`,
		"sec-ch-ua-bitness": `"${hints.bitness}"`,
		"sec-ch-ua-wow64": "?0",
		"sec-ch-ua-model": '""',
		"sec-ch-ua-form-factors": '"Desktop"',
		"sec-ch-ua-mobile": hints.mobile ? "?1" : "?0",
	};
}
