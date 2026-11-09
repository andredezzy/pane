import { existsSync, realpathSync } from "node:fs";

// Waiting on processes by pid, and stopping those a scenario did not start
// itself: an app that starts itself again (the panic wipe) is no child of the
// runner, but its arguments still name the scenario's data directory.

export function isRunning(pid: number): boolean {
	try {
		process.kill(pid, 0);

		return true;
	} catch (error) {
		// EPERM: there, but another user's.
		return (error as NodeJS.ErrnoException).code === "EPERM";
	}
}

// Whether `pid` is gone within `timeoutMs`.
export async function waitForExit(
	pid: number,
	timeoutMs = 10_000,
): Promise<boolean> {
	const deadline = Date.now() + timeoutMs;

	while (isRunning(pid)) {
		if (Date.now() > deadline) {
			return false;
		}

		await Bun.sleep(100);
	}

	return true;
}

// The pids pgrep lists for these arguments.
export async function pgrep(...args: string[]): Promise<number[]> {
	const output = await Bun.$`/usr/bin/pgrep ${args}`.quiet().nothrow().text();

	return output.split("\n").filter(Boolean).map(Number);
}

// The pid of the app's helper of `kind` ("network.mojom"): the one process
// whose arguments name it and the app's data.
export async function helperPid(
	dataDir: string,
	kind: string,
): Promise<number> {
	const rows = (await Bun.$`ps -axo pid=,args=`.text()).split("\n");

	return Number(
		rows
			.find(
				(row) =>
					row.includes(kind) && row.includes(dataDir.replace(/^\/private/, "")),
			)
			?.trim()
			.split(/\s+/)[0],
	);
}

// Whether a volume is mounted at `path`, which the table may name under /private.
export async function mountedAt(path: string): Promise<boolean> {
	const table = await Bun.$`/sbin/mount`.text();

	return (
		table.includes(` on ${path} `) || table.includes(` on /private${path} `)
	);
}

// Kills every process whose arguments contain `path`, and waits for them to
// go, so nothing writes into the directory as it is removed. Returns their
// pids.
export async function killUsing(path: string): Promise<number[]> {
	const pattern = path.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");

	const pids = (await pgrep("-f", pattern)).filter(
		(pid) => pid !== process.pid,
	);

	for (const pid of pids) {
		try {
			process.kill(pid, "SIGKILL");
		} catch {}
	}

	await Promise.all(pids.map((pid) => waitForExit(pid, 5_000)));

	return pids;
}

// The devices of the disk images attached from inside `path`: a scenario's
// data volume (ADR 0113). hdiutil names an image by the path it was attached
// with, /var or /private/var, so both sides are resolved.
export async function imagesAttachedIn(path: string): Promise<string[]> {
	if (!existsSync(path)) {
		return [];
	}

	const inside = `${realpathSync(path)}/`;

	const resolved = (image: string) =>
		existsSync(image) ? realpathSync(image) : image;

	const info = await Bun.$`hdiutil info -plist | plutil -convert json -o - -`
		.quiet()
		.nothrow();

	const images =
		(
			JSON.parse(info.stdout.toString() || "{}") as {
				images?: {
					"image-path"?: string;
					"system-entities"?: {
						"dev-entry"?: string;
						"content-hint"?: string;
					}[];
				}[];
			}
		).images ?? [];

	return images
		.filter((image) => {
			const imagePath = image["image-path"];

			return imagePath !== undefined && resolved(imagePath).startsWith(inside);
		})
		.map((image) => imageDevice(image["system-entities"] ?? []))
		.filter((device): device is string => device !== undefined);
}

// An attached image's own device: its whole disk, not its APFS container's,
// a whole disk too. hdiutil lists them in no set order (DataVolume).
export function imageDevice(
	entities: { "dev-entry"?: string; "content-hint"?: string }[],
): string | undefined {
	return entities.find(
		(entity) =>
			/^\/dev\/disk\d+$/.test(entity["dev-entry"] ?? "") &&
			![
				"Apple_APFS_Container",
				"EF57347C-0000-11AA-AA11-00306543ECAC",
			].includes(entity["content-hint"] ?? ""),
	)?.["dev-entry"];
}

// Detaches them: a Pane killed leaves its volume attached until its
// watchdog detaches it, and killUsing would kill that watchdog, whose
// arguments name the image.
export async function detachImagesIn(path: string): Promise<void> {
	for (const device of await imagesAttachedIn(path)) {
		await Bun.$`hdiutil detach ${device} -force -quiet`.quiet().nothrow();
	}
}

const LSREGISTER =
	"/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister";

// A launch registers the bundle with LaunchServices, which opens the newest
// copy of a bundle identifier it knows: a test copy newer than the installed
// Pane would be the one a link opens, on the user's own data. A run forgets
// the copy it ran when it ends; one in /Applications is the user's.
export async function forgetBundle(binary: string): Promise<void> {
	const bundle = binary.replace(/\/Contents\/MacOS\/[^/]+$/, "");

	if (bundle === binary || bundle.startsWith("/Applications/")) {
		return;
	}

	await Bun.$`${LSREGISTER} -u ${bundle}`.quiet().nothrow();
}
