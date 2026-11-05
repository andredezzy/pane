// What a process that only reads files finds of a profile while Pane is
// unlocked (docs/plans/engine-file-encryption.md): every file under the
// profile's folder, its caches' folder, the data root, the throwaway home, and
// what Pane's processes made in $TMPDIR, each sorted into what wrote it, what
// its content shows, and whether the plan's allow-list lets it stay plain.
import { createHash } from "node:crypto";
import { type Dirent, readdirSync, readFileSync, realpathSync } from "node:fs";
import { join, relative } from "node:path";

export type Planted = {
	// Exact strings the session planted, each random: found in no file.
	values: Record<string, string>;
	// The sites and pages it visited: named by no folder or file, in clear or
	// as an unkeyed hash.
	hosts: string[];
	origins: string[];
	urls: string[];
};

export type AuditedFile = {
	path: string;
	size: number;
	kind: string;
	writer: string;
	// Why the plan lets it stay plain; null for a file that must be ciphertext.
	allowed: string | null;
	// Why its content reads as plain, if it does.
	plain: string[];
	entropy: number | null;
	// Each planted value it holds, by name.
	planted: string[];
	// Each site or page its name gives away, and how.
	named: string[];
};

export type Audit = {
	files: AuditedFile[];
	directories: { path: string; named: string[] }[];
	// Files that must be ciphertext and read as plain.
	plain: AuditedFile[];
	// Files holding a planted value, allow-listed ones included.
	planted: AuditedFile[];
	// Names that give a site away.
	named: { path: string; named: string[] }[];
};

const SQLITE = Buffer.from("SQLite format 3\0", "latin1");
const LEVELDB_FOOTER = Buffer.from("57fb808b247547db", "hex");
const SIMPLE_CACHE = Buffer.from("305c72a71b6dfbfc", "hex");

// The plan's allow-list, relative to the data root (Engine's own files) and,
// for a profile, to its folder.
const DATA_ROOT_ALLOWED: [RegExp, string][] = [
	[
		/^(settings|security|Data\.security|migration|profiles|deletions)\.json$/,
		"Pane's own file",
	],
	[/^Pane\.lock$/, "the data's lock"],
	[/^Data\.sparsebundle\//, "the volume's image"],
	[
		/^Engine\/(Local State|Last Version|Variations|ChromeFeatureState|First Run|Last Browser|lockfile|SingletonCookie|SingletonLock|SingletonSocket|DevToolsActivePort|CrashpadMetrics(-active)?\.pma|BrowserMetrics(-active)?\.pma|.*\.pma)$/,
		"the engine's root, before the PIN",
	],
	[
		/^Engine\/(GraphiteDawnCache|GPUPersistentCache|ShaderCache|GrShaderCache|DawnGraphiteCache|DawnWebGPUCache|Safe Browsing|component_crx_cache|extensions_crx_cache|Webstore Downloads|NativeMessagingHosts|Default|Crash Reports|BrowserMetrics|CertificateTransparency|Crowd Deny|OnDeviceHeadSuggestModel|PKIMetadata|Subresource Filter|SSLErrorAssistant|TpcdMetadata|TrustTokenKeyCommitments|ZxcvbnData|MEIPreload|FirstPartySetsPreloaded|Dictionaries|Floc|hyphen-data|OriginTrials|WidevineCdm|segmentation_platform|AutofillStates|CookieReadinessList|OpenCookieDatabase|PrivacySandboxAttestationsPreloaded|ProbabilisticRevealTokenRegistry|RecoveryImproved|Trust Protection Lists|ActorSafetyLists|AmountExtractionHeuristicRegexes|commerce_heuristics|optimization_guide_model_store|Local Traces)\//,
		"the engine's root, before the PIN",
	],
	[/^automation\//, "the scenario's channel"],
	[/^no-electron-app/, "the scenario's stand-in"],
];

const PROFILE_ALLOWED: [RegExp, string][] = [
	[/^Extensions\//, "installed extensions' public code"],
];

// What wrote a file of a profile, from its path, as the plan's table sorts them.
const KINDS: [RegExp, string, string][] = [
	[
		/^(Network\/)?(Cookies|Reporting and NEL|Trust Tokens|Device Bound Sessions)(-journal|-wal)?$/,
		"SQLite",
		"network service",
	],
	[
		/^(Network\/)?(Network Persistent State|SCT Auditing Pending Reports|TransportSecurity)$/,
		"loose",
		"network service",
	],
	[/^Shared Dictionary\//, "cache", "network service"],
	[
		/^(Cache|Code Cache|GPUCache|DawnWebGPUCache|DawnGraphiteCache)\//,
		"cache",
		"network service or browser",
	],
	[/^(LOG|LOCK)$/, "empty", "browser"],
	[/^Service Worker\/(CacheStorage|ScriptCache)\//, "cache", "browser"],
	[/^Service Worker\/Database\//, "LevelDB", "browser"],
	[/^(Local Storage|Session Storage)\//, "LevelDB", "Storage Service"],
	[/^IndexedDB\/.*\.blob\//, "stream", "browser (blob)"],
	[/^IndexedDB\//, "LevelDB", "browser"],
	[/^WebStorage\/.*\/IndexedDB\/.*\.blob\//, "stream", "browser (blob)"],
	[/^WebStorage\/.*\/IndexedDB\//, "LevelDB", "browser"],
	[/^WebStorage\/.*\/CacheStorage\//, "cache", "browser"],
	[/^WebStorage\//, "SQLite or LevelDB", "browser"],
	[/^File System\/(.*\/Paths|Origins)\//, "LevelDB", "browser"],
	[/^File System\//, "stream", "browser, renderers"],
	[
		/^(shared_proto_db|GCM Store|Sync Data|Site Characteristics Database|Platform Notifications|AutofillAiModelCache|ClientCertificates|discount_infos_db|Local Extension Settings|Sync Extension Settings|Managed Extension Settings|Extension State|Extension Rules|Extension Scripts|databases|BudgetDatabase|commerce_subscription_db|coupon_db|chrome_cart_db|discounts_db|parcel_tracking_db|Feature Engagement Tracker|Download Service|optimization_guide|AutofillStrikeDatabase|DataSharing|Segmentation Platform|PersistentOriginTrials|WebrtcVideoStats|VideoDecodeStats)\//,
		"LevelDB",
		"browser",
	],
	[
		/^(History|Favicons|Top Sites|Shortcuts|Web Data|Account Web Data|Login Data( For Account)?|Network Action Predictor|Affiliation Database|QuotaManager|DIPS|CdmStorage\.db|heavy_ad_intervention_opt_out\.db|declarative_performance_observer\.db|MediaDeviceSalts|ServerCertificate|PrivateVerificationTokens|Extension Cookies|Conversions|InterestGroups|PrivateAggregation|SharedStorage|BrowsingTopicsSiteData|Attribution Reporting|first_party_sets\.db|trusted_vault\.pb|LOCK)(-journal|-wal|-shm)?$/,
		"SQLite",
		"browser",
	],
	[
		/^(Preferences|Secure Preferences|AccountPreferences|BookmarkMergedSurfaceOrdering|PreferredApps|README|Bookmarks(\.bak)?|Web Applications\/|Custom Dictionary\.txt|DNR Extension Rules\/|.*\.json$)/,
		"loose",
		"browser",
	],
	[
		/^(Sessions|Sessions_Encrypted|EncryptedBookmarks2)\//,
		"sessions",
		"browser",
	],
	[/^blob_storage\//, "memory-only target", "browser"],
	[/^Extensions\//, "extension code", "browser, unzip utility"],
];

function classify(path: string): { kind: string; writer: string } {
	for (const [pattern, kind, writer] of KINDS) {
		if (pattern.test(path)) {
			return { kind, writer };
		}
	}

	return { kind: "unknown", writer: "unknown" };
}

// Bits a byte over the whole file.
export function entropyOf(bytes: Uint8Array): number {
	if (bytes.length === 0) {
		return 0;
	}

	const counts = new Array<number>(256).fill(0);

	for (const byte of bytes) {
		counts[byte] = (counts[byte] ?? 0) + 1;
	}

	let entropy = 0;

	for (const count of counts) {
		if (count > 0) {
			const share = count / bytes.length;
			entropy -= share * Math.log2(share);
		}
	}

	return entropy;
}

const textShare = (bytes: Uint8Array) => {
	let text = 0;

	for (const byte of bytes) {
		if (
			(byte >= 0x20 && byte < 0x7f) ||
			byte === 9 ||
			byte === 10 ||
			byte === 13
		) {
			text++;
		}
	}

	return bytes.length > 0 ? text / bytes.length : 0;
};

// Why this content reads as plain, if it does: a store's own header, JSON,
// HTML, a URL, or, past 4 KiB, too little entropy for ciphertext.
export function plainMarkers(bytes: Buffer): string[] {
	const found: string[] = [];

	if (bytes.length === 0) {
		return found;
	}

	if (bytes.subarray(0, SQLITE.length).equals(SQLITE)) {
		found.push("SQLite header");
	}

	if (bytes.length >= 8 && bytes.subarray(-8).equals(LEVELDB_FOOTER)) {
		found.push("LevelDB table footer");
	}

	if (bytes.length >= 8 && bytes.subarray(0, 8).equals(SIMPLE_CACHE)) {
		found.push("Simple cache header");
	}

	const head = bytes.subarray(0, 512).toString("latin1").trimStart();

	if (/^[{[]\s*["{[\]}]/.test(head)) {
		found.push("JSON");
	}

	if (/^<(!doctype|html|head|script)/i.test(head)) {
		found.push("HTML");
	}

	if (/https?:\/\/[a-z0-9-]+\.[a-z0-9.-]+/i.test(bytes.toString("latin1"))) {
		found.push("a URL");
	}

	if (bytes.length > 4096 && entropyOf(bytes) < 7.9) {
		found.push("low entropy");
	}

	if (bytes.length <= 4096 && bytes.length >= 16 && textShare(bytes) > 0.75) {
		found.push("text");
	}

	return found;
}

const hex = (algorithm: string, text: string) =>
	createHash(algorithm).update(text).digest("hex");

// Every unkeyed hash a store could name a site by: SHA-1, SHA-256 and MD5 of
// each form of its address, and the Simple cache's entry hash (the first 8
// bytes of the SHA-1, in little-endian order, of the entry's key).
function unkeyedNames(planted: Planted): Map<string, string> {
	const names = new Map<string, string>();

	const add = (label: string, text: string) => {
		for (const algorithm of ["sha1", "sha256", "md5"]) {
			names.set(hex(algorithm, text), `${algorithm} of ${label}`);
		}

		const sha1 = createHash("sha1").update(text).digest();
		const entry = Buffer.from(sha1.subarray(0, 8)).reverse().toString("hex");
		names.set(entry, `the Simple cache's hash of ${label}`);
	};

	for (const host of planted.hosts) {
		add(host, host);
	}

	for (const origin of planted.origins) {
		add(origin, origin);
		add(`${origin}/`, `${origin}/`);

		// Storage's identifier for it (GetIdentifierFromOrigin), which CacheStorage's
		// folders hash: http_host_port.
		const { protocol, hostname, port } = new URL(origin);
		const identifier = `${protocol.slice(0, -1)}_${hostname}_${port || 0}`;
		add(identifier, identifier);
	}

	for (const url of planted.urls) {
		add(url, url);

		// The HTTP cache's key, with its network isolation key's site.
		for (const origin of planted.origins) {
			const site = origin.replace(/:\d+$/, "");
			add(`${url} (cache key)`, `1/0/_dk_${site} ${site} ${url}`);
		}
	}

	return names;
}

function namedBy(
	path: string,
	planted: Planted,
	hashes: Map<string, string>,
): string[] {
	const found: string[] = [];
	const name = path.toLowerCase();

	for (const host of planted.hosts) {
		if (name.includes(host.toLowerCase())) {
			found.push(`names ${host}`);
		}
	}

	for (const part of name.split("/")) {
		for (const piece of part.split(/[^0-9a-f]+/)) {
			const label = hashes.get(piece);

			if (label) {
				found.push(label);
			}
		}
	}

	return found;
}

function* walk(root: string): Generator<{ path: string; entry: Dirent }> {
	let entries: Dirent[];

	try {
		entries = readdirSync(root, { withFileTypes: true });
	} catch {
		return;
	}

	for (const entry of entries) {
		const path = join(root, entry.name);
		yield { path, entry };

		if (entry.isDirectory()) {
			yield* walk(path);
		}
	}
}

const heldBy = (bytes: Buffer, planted: Planted) =>
	Object.entries(planted.values)
		.filter(
			([, value]) =>
				bytes.includes(value, 0, "latin1") ||
				bytes.includes(value, 0, "utf16le"),
		)
		.map(([name]) => name);

// Audits every file under `root`, its paths relative to it. `allowed` says why
// a path may stay plain, or null.
function auditTree(
	root: string,
	planted: Planted,
	hashes: Map<string, string>,
	allowed: (path: string) => string | null,
	classifyPath: (path: string) => { kind: string; writer: string },
	label: (path: string) => string,
): { files: AuditedFile[]; directories: Audit["directories"] } {
	const files: AuditedFile[] = [];
	const directories: Audit["directories"] = [];

	for (const { path, entry } of walk(root)) {
		const name = relative(root, path);

		if (entry.isDirectory()) {
			directories.push({
				path: label(name),
				named: namedBy(name, planted, hashes),
			});

			continue;
		}

		if (!entry.isFile()) {
			continue;
		}

		let bytes: Buffer;

		try {
			bytes = readFileSync(path);
		} catch {
			continue;
		}

		files.push({
			path: label(name),
			size: bytes.length,
			...classifyPath(name),
			allowed: bytes.length === 0 ? "empty" : allowed(name),
			plain: plainMarkers(bytes),
			entropy:
				bytes.length > 4096 ? Math.round(entropyOf(bytes) * 1000) / 1000 : null,
			planted: heldBy(bytes, planted),
			named: namedBy(name, planted, hashes),
		});
	}

	return { files, directories };
}

export type AuditRoots = {
	// The data root: Pane's own files, Engine, and the mounted volume in it.
	dataRoot: string;
	// The throwaway home, for what is not under the data root: the caches.
	home: string;
	// Folders Pane's processes write to outside both: its temporary folder.
	temporary: string[];
};

export function audit(roots: AuditRoots, planted: Planted): Audit {
	const hashes = unkeyedNames(planted);
	const dataRoot = realpathSync(roots.dataRoot);
	const home = realpathSync(roots.home);
	const files: AuditedFile[] = [];
	const directories: Audit["directories"] = [];

	const add = (tree: {
		files: AuditedFile[];
		directories: Audit["directories"];
	}) => {
		files.push(...tree.files);
		directories.push(...tree.directories);
	};

	const inVolume = /^Engine\/Volume\/[^/]+\/(Profile|Cache)\/(.*)$/;

	add(
		auditTree(
			dataRoot,
			planted,
			hashes,
			(path) => {
				const inProfile = inVolume.exec(path);

				if (inProfile) {
					const inside = inProfile[2] ?? "";

					return (
						PROFILE_ALLOWED.find(([pattern]) => pattern.test(inside))?.[1] ??
						null
					);
				}

				return (
					DATA_ROOT_ALLOWED.find(([pattern]) => pattern.test(path))?.[1] ?? null
				);
			},
			(path) => {
				const inProfile = inVolume.exec(path);

				if (inProfile) {
					return classify(inProfile[2] ?? "");
				}

				return { kind: "data root", writer: "Pane or the engine" };
			},
			(path) => `<data>/${path}`,
		),
	);

	// The home beside the data root: the caches' links, macOS's and Crashpad's
	// folders, and the Downloads folder, the user's files (out of scope).
	const isDataRoot = (path: string) =>
		join(home, path) === dataRoot ||
		join(home, path).startsWith(`${dataRoot}/`);

	const homeTree = auditTree(
		home,
		planted,
		hashes,
		(path) => {
			if (path.startsWith("Downloads/")) {
				return "the user's download";
			}

			return path.startsWith("Library/Preferences/")
				? "macOS's preferences of the throwaway home"
				: null;
		},
		() => ({ kind: "home", writer: "Pane, its engine, or macOS" }),
		(path) => `<home>/${path}`,
	);

	add({
		files: homeTree.files.filter(
			(file) => !isDataRoot(file.path.slice("<home>/".length)),
		),
		directories: homeTree.directories.filter(
			(directory) => !isDataRoot(directory.path.slice("<home>/".length)),
		),
	});

	for (const folder of roots.temporary) {
		add(
			auditTree(
				realpathSync(folder),
				planted,
				hashes,
				() => null,
				() => ({ kind: "temporary", writer: "Pane's processes" }),
				(path) => `<tmp>/${path}`,
			),
		);
	}

	return {
		files,
		directories,
		plain: files.filter(
			(file) => file.allowed === null && file.plain.length > 0,
		),
		planted: files.filter((file) => file.planted.length > 0),
		named: [...files, ...directories]
			.filter((item) => item.named.length > 0)
			.map(({ path, named }) => ({ path, named })),
	};
}
