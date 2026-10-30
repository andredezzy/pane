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

// Kills every process whose arguments contain `path`, and waits for them to
// go, so nothing writes into the directory as it is removed. Returns their
// pids.
export async function killUsing(path: string): Promise<number[]> {
	const pattern = path.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");

	const pids = (await Bun.$`pgrep -f ${pattern}`.quiet().nothrow().text())
		.split("\n")
		.filter(Boolean)
		.map(Number)
		.filter((pid) => pid !== process.pid);

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
					"system-entities"?: { "dev-entry"?: string }[];
				}[];
			}
		).images ?? [];

	return images
		.filter((image) => {
			const imagePath = image["image-path"];

			return imagePath !== undefined && resolved(imagePath).startsWith(inside);
		})
		.map((image) => image["system-entities"]?.[0]?.["dev-entry"])
		.filter((device): device is string => device !== undefined);
}

// Detaches them: a Pane killed leaves its volume attached until its
// watchdog detaches it, and killUsing would kill that watchdog, whose
// arguments name the image.
export async function detachImagesIn(path: string): Promise<void> {
	for (const device of await imagesAttachedIn(path)) {
		await Bun.$`hdiutil detach ${device} -force -quiet`.quiet().nothrow();
	}
}
