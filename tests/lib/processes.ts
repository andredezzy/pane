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
