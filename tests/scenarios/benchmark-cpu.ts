import {
	type BenchmarkedApp,
	benchmarkedApps,
	conditions,
	inTurn,
	launchApp,
	processesOf,
	stats,
} from "../lib/benchmark";
import { swiftTool } from "../lib/tools";
import type { Scenario } from "../run";

// Idle CPU, the Electron app against the native one: one tab on a small
// local page, nothing happening, every process of the app summed over a
// minute, after a minute and a half's rest (Chromium's own work 30 s and
// 60 s after startup falls before it). The kernel's own counters
// (proc_pid_rusage, tools/process-usage.swift) are read every second: CPU
// time to the nanosecond, idle and interrupt wakeups, and its estimate of
// the energy each process spent on the CPU. ps and top round CPU to 10 ms
// and 0.1 %, coarser than an idle app's use. The native app runs without
// its automation channel, which polls ten times a second.

const RUNS = 5;
const IDLE_MS = 90_000;
const SECONDS = 60;

type Counters = {
	pid: number;
	cpuNs: number;
	idleWakeups: number;
	interruptWakeups: number;
	energyNj: number;
};

type Reading = {
	at: number;
	processes: Counters[];
	// Each process's type (browser, renderer, gpu-process, ...).
	types: Record<number, string>;
};

async function read(app: BenchmarkedApp): Promise<Reading> {
	const processes = await processesOf(app);
	const tool = await swiftTool("process-usage");

	const reading = JSON.parse(
		await Bun.$`${tool} ${processes.map((entry) => String(entry.pid))}`.text(),
	) as Omit<Reading, "types">;

	return {
		...reading,
		types: Object.fromEntries(
			processes.map((entry) => [entry.pid, entry.type]),
		),
	};
}

type Totals = Omit<Counters, "pid">;

// What the app's processes, or those `of` picks, used between two readings;
// one that started in between counts from its start.
function between(
	start: Reading,
	end: Reading,
	of: (pid: number) => boolean = () => true,
): Totals {
	const before = new Map(start.processes.map((entry) => [entry.pid, entry]));

	const totals: Totals = {
		cpuNs: 0,
		idleWakeups: 0,
		interruptWakeups: 0,
		energyNj: 0,
	};

	for (const entry of end.processes.filter((process) => of(process.pid))) {
		const earlier = before.get(entry.pid);

		totals.cpuNs += entry.cpuNs - (earlier?.cpuNs ?? 0);
		totals.idleWakeups += entry.idleWakeups - (earlier?.idleWakeups ?? 0);

		totals.interruptWakeups +=
			entry.interruptWakeups - (earlier?.interruptWakeups ?? 0);

		totals.energyNj += entry.energyNj - (earlier?.energyNj ?? 0);
	}

	return totals;
}

// Per second, as rates: percent of one core, wakeups, and milliwatts.
function rates(totals: Totals, milliseconds: number) {
	const seconds = milliseconds / 1000;

	return {
		cpuPercent: (totals.cpuNs / 1e9 / seconds) * 100,
		idleWakeups: totals.idleWakeups / seconds,
		interruptWakeups: totals.interruptWakeups / seconds,
		milliwatts: totals.energyNj / 1e6 / seconds,
	};
}

async function idleMinute(app: BenchmarkedApp) {
	const first = await read(app);
	const readings = [first];

	// Each reading a second after the last was due, so the minute does not
	// drift by the readings' own time.
	for (let second = 1; second <= SECONDS; second++) {
		await Bun.sleep(Math.max(0, first.at + second * 1000 - Date.now()));
		readings.push(await read(app));
	}

	const last = readings.at(-1) as Reading;

	const perSecond = readings.slice(1).map((reading, index) => {
		const previous = readings[index] as Reading;

		return rates(between(previous, reading), reading.at - previous.at);
	});

	const types = [...new Set(Object.values(last.types))];

	return {
		elapsedMs: last.at - first.at,
		processesBefore: first.processes.length,
		processesAfter: last.processes.length,
		totals: between(first, last),
		byType: Object.fromEntries(
			types.map((type) => [
				type,
				rates(
					between(first, last, (pid) => last.types[pid] === type),
					last.at - first.at,
				),
			]),
		),
		...rates(between(first, last), last.at - first.at),
		perSecondCpuPercent: stats(perSecond.map((second) => second.cpuPercent)),
		perSecond,
	};
}

const benchmarkCpu: Scenario = async (context) => {
	const started = conditions();
	const apps = await benchmarkedApps(context);
	await swiftTool("process-usage");

	const minutes: Record<string, Awaited<ReturnType<typeof idleMinute>>[]> =
		Object.fromEntries(apps.map((app) => [app.kind, []]));

	for (let run = 0; run < RUNS; run++) {
		for (const app of inTurn(apps, run)) {
			const running = await launchApp(context, app, {
				url: context.fixtures.url(`smoke.html?idle=${run}`),
			});

			await Bun.sleep(IDLE_MS);
			minutes[app.kind]?.push(await idleMinute(app));
			await running.quit();
			await Bun.sleep(2_000);
		}
	}

	return {
		runs: RUNS,
		idleMs: IDLE_MS,
		seconds: SECONDS,
		conditions: { started, ended: conditions() },
		summary: Object.fromEntries(
			Object.entries(minutes).map(([kind, runs]) => [
				kind,
				{
					cpuPercent: stats(runs.map((run) => run.cpuPercent)),
					idleWakeups: stats(runs.map((run) => run.idleWakeups)),
					interruptWakeups: stats(runs.map((run) => run.interruptWakeups)),
					milliwatts: stats(runs.map((run) => run.milliwatts)),
				},
			]),
		),
		minutes,
	};
};

export default benchmarkCpu;
