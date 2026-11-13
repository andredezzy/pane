import { readFileSync } from "node:fs";
import type { Subprocess } from "bun";
import { bySystem, noWindowsSide, powershell } from "./platform";

// Waiting on processes by pid, and stopping those a scenario did not start
// itself: an app that starts itself again (the panic wipe) is no child of the
// runner, but its arguments still name the scenario's data directory. The
// Mac's side asks ps and pgrep; Windows' asks its process table (Win32_Process,
// through PowerShell) and stops a tree with taskkill.

// Signal 0 tests that a process is there, on both systems.
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
async function pgrep(...args: string[]): Promise<number[]> {
	const output = await Bun.$`/usr/bin/pgrep ${args}`.quiet().nothrow().text();

	return output.split("\n").filter(Boolean).map(Number);
}

type WindowsProcess = {
	ProcessId: number;
	ParentProcessId: number;
	Name: string;
	ExecutablePath: string | null;
	CommandLine: string | null;
	WorkingSetSize: number;
};

// Windows' process table, every process with its command line (null where
// Windows won't say: another user's).
async function windowsProcesses(): Promise<WindowsProcess[]> {
	return JSON.parse(
		await powershell(
			"ConvertTo-Json -Compress -InputObject @(Get-CimInstance Win32_Process | Select-Object ProcessId, ParentProcessId, Name, ExecutablePath, CommandLine, WorkingSetSize)",
		),
	) as WindowsProcess[];
}

// The pids whose command line matches `pattern`, an extended regular
// expression, as pgrep -f reads it.
export const pidsMatching = bySystem({
	mac: (pattern: string) => pgrep("-f", pattern),
	windows: async (pattern: string) => {
		const matches = new RegExp(pattern);

		return (await windowsProcesses())
			.filter((row) => matches.test(row.CommandLine ?? ""))
			.map((row) => row.ProcessId);
	},
});

// The pids of the processes `pid` started.
export const childrenOf = bySystem({
	mac: (pid: number) => pgrep("-P", String(pid)),
	windows: async (pid: number) =>
		(await windowsProcesses())
			.filter((row) => row.ParentProcessId === pid)
			.map((row) => row.ProcessId),
});

// Whether a process of the app named `name` runs ("Maps": Maps.exe on
// Windows).
export const isRunningNamed = bySystem({
	mac: async (name: string) => (await pgrep("-x", name)).length > 0,
	windows: async (name: string) =>
		(await windowsProcesses()).some(
			(row) => row.Name.toLowerCase() === `${name}.exe`.toLowerCase(),
		),
});

// Each process's pid and command line.
const commandLines = bySystem({
	mac: async () =>
		(await Bun.$`ps -axo pid=,args=`.text()).split("\n").flatMap((row) => {
			const match = /^\s*(\d+)\s+(.*)$/.exec(row);

			return match ? [{ pid: Number(match[1]), command: match[2] ?? "" }] : [];
		}),
	windows: async () =>
		(await windowsProcesses()).map((row) => ({
			pid: row.ProcessId,
			command: row.CommandLine ?? "",
		})),
});

// The pid of the app's helper of `kind` ("network.mojom"): the one process
// whose arguments name it and the app's data. The Mac's temporary folder is
// /private/var, which its helpers name /var.
export async function helperPid(
	dataDir: string,
	kind: string,
): Promise<number> {
	const data = dataDir.replace(/^\/private/, "");

	return (
		(await commandLines()).find(
			(row) => row.command.includes(kind) && row.command.includes(data),
		)?.pid ?? Number.NaN
	);
}

// Stops `pid` at once; on Windows, with the processes it started, which no
// process group ends with it there.
const kill = bySystem({
	mac: async (pid: number) => {
		try {
			process.kill(pid, "SIGKILL");
		} catch {}
	},
	windows: async (pid: number) => {
		await Bun.spawn(["taskkill", "/F", "/T", "/PID", String(pid)], {
			stdout: "ignore",
			stderr: "ignore",
		}).exited;
	},
});

// Kills every process whose arguments contain `path`, and waits for them to
// go, so nothing writes into the directory as it is removed. Returns their
// pids.
export async function killUsing(path: string): Promise<number[]> {
	const pattern = path.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");

	const pids = (await pidsMatching(pattern)).filter(
		(pid) => pid !== process.pid,
	);

	await Promise.all(pids.map((pid) => kill(pid)));
	await Promise.all(pids.map((pid) => waitForExit(pid, 5_000)));

	return pids;
}

// The process group `pid` is in, as ps writes it; empty when it is gone.
// Windows has none: what a kill of a group ends there is a tree (taskkill
// /T), so its side names the tree `pid` is in by its oldest living ancestor.
export const processGroupOf = bySystem({
	mac: async (pid: number | undefined) =>
		(
			await Bun.$`/bin/ps -o pgid= -p ${pid ?? ""}`.quiet().nothrow().text()
		).trim(),
	windows: async (pid: number | undefined): Promise<string> => {
		const table = await windowsProcesses();

		const parents = new Map(
			table.map((row) => [row.ProcessId, row.ParentProcessId] as const),
		);

		if (pid === undefined || !parents.has(pid)) {
			return "";
		}

		let root = pid;
		const seen = new Set([root]);

		for (
			let parent = parents.get(root);
			parent !== undefined && parents.has(parent) && !seen.has(parent);
			parent = parents.get(root)
		) {
			seen.add(parent);
			root = parent;
		}

		return String(root);
	},
});

// Another process's environment on Windows, read where the system keeps it,
// in that process's memory: its PEB's process parameters (Environment at
// 0x80, EnvironmentSize at 0x3F0 on 64-bit Windows 10 and later). Null where
// it won't open.
const ENVIRONMENT_READER = `Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class PaneEnvironment {
  [StructLayout(LayoutKind.Sequential)]
  struct BasicInformation {
    public IntPtr ExitStatus;
    public IntPtr PebBaseAddress;
    public IntPtr AffinityMask;
    public IntPtr BasePriority;
    public IntPtr UniqueProcessId;
    public IntPtr InheritedFromUniqueProcessId;
  }

  [DllImport("ntdll.dll")]
  static extern int NtQueryInformationProcess(IntPtr process, int informationClass, ref BasicInformation information, int length, out int returned);
  [DllImport("kernel32.dll")]
  static extern IntPtr OpenProcess(int access, bool inherit, int pid);
  [DllImport("kernel32.dll")]
  static extern bool CloseHandle(IntPtr handle);
  [DllImport("kernel32.dll")]
  static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] buffer, IntPtr size, out IntPtr read);

  static long Read64(IntPtr process, IntPtr address) {
    byte[] bytes = new byte[8];
    IntPtr read;
    return ReadProcessMemory(process, address, bytes, (IntPtr)8, out read) ? BitConverter.ToInt64(bytes, 0) : 0;
  }

  public static string Of(int pid) {
    IntPtr process = OpenProcess(0x0400 | 0x0010, false, pid);
    if (process == IntPtr.Zero) return null;
    try {
      BasicInformation information = new BasicInformation();
      int returned;
      if (NtQueryInformationProcess(process, 0, ref information, Marshal.SizeOf(information), out returned) != 0) return null;
      long parameters = Read64(process, information.PebBaseAddress + 0x20);
      if (parameters == 0) return null;
      long environment = Read64(process, (IntPtr)(parameters + 0x80));
      long size = Read64(process, (IntPtr)(parameters + 0x3F0));
      if (environment == 0 || size <= 0 || size > (1 << 24)) return null;
      byte[] block = new byte[size];
      IntPtr read;
      if (!ReadProcessMemory(process, (IntPtr)environment, block, (IntPtr)size, out read)) return null;
      return Encoding.Unicode.GetString(block).Replace((char)0, ' ').Trim();
    } finally {
      CloseHandle(process);
    }
  }
}
'@`;

// Every process's pid, command line, and environment, one row each, for
// what must be on neither (engine-keys).
export const commandLinesAndEnvironments = bySystem({
	mac: async () =>
		(await Bun.$`ps -E -ww -axo pid=,command=`.text()).split("\n"),
	windows: async (): Promise<string[]> =>
		(
			JSON.parse(
				await powershell(`${ENVIRONMENT_READER}
ConvertTo-Json -Compress -InputObject @(Get-CimInstance Win32_Process | ForEach-Object {
  [pscustomobject]@{ pid = $_.ProcessId; command = $_.CommandLine; environment = [PaneEnvironment]::Of($_.ProcessId) }
})`),
			) as {
				pid: number;
				command: string | null;
				environment: string | null;
			}[]
		).map((row) => `${row.pid} ${row.command ?? ""} ${row.environment ?? ""}`),
});

// Where a stuck process's threads are, for 2 s, written to `file`; a sample
// that fails writes nothing.
export const sampleProcess = bySystem({
	mac: async (pid: number, file: string) => {
		await Bun.$`sample ${pid} 2 -file ${file}`.quiet().nothrow();
	},
	windows: async (_pid: number, _file: string): Promise<void> => {
		throw new Error(
			"Windows ships no sampler of a process's stacks (the Mac's sample)",
		);
	},
});

// A process holding `file` open, as another app would, until it is killed: a
// file of the volume kept busy (ADR 0113).
export const holdOpen = bySystem({
	mac: (file: string): Subprocess =>
		Bun.spawn(["/usr/bin/tail", "-f", file], { stdout: "ignore" }),
	windows: (_file: string): Subprocess => {
		throw noWindowsSide(
			"a file of the volume held open",
			"U5",
			"Windows has no volume",
		);
	},
});

export type ProcessRow = {
	pid: number;
	// The executable's full path.
	executable: string;
	command: string;
	rssBytes: number;
};

// Every process, with its executable, command line, and resident memory
// (Windows' working set).
export const processTable = bySystem({
	mac: async (): Promise<ProcessRow[]> => {
		const commands = new Map(
			(await Bun.$`ps -axww -o pid=,command=`.quiet().text())
				.split("\n")
				.flatMap((line) => {
					const match = /^\s*(\d+)\s+(.*)$/.exec(line);

					return match ? [[Number(match[1]), match[2] ?? ""] as const] : [];
				}),
		);

		return (await Bun.$`ps -axww -o pid=,rss=,comm=`.quiet().text())
			.split("\n")
			.flatMap((line) => {
				const match = /^\s*(\d+)\s+(\d+)\s+(.*)$/.exec(line);

				if (!match) {
					return [];
				}

				const pid = Number(match[1]);
				const executable = match[3] ?? "";

				return [
					{
						pid,
						executable,
						command: commands.get(pid) ?? executable,
						rssBytes: Number(match[2]) * 1024,
					},
				];
			});
	},
	windows: async (): Promise<ProcessRow[]> =>
		(await windowsProcesses()).map((row) => ({
			pid: row.ProcessId,
			executable: row.ExecutablePath ?? row.Name,
			command: row.CommandLine ?? row.ExecutablePath ?? row.Name,
			rssBytes: row.WorkingSetSize,
		})),
});

export type Footprints = {
	"total footprint": number;
	processes: { pid: number; footprint: number }[];
	errors: unknown[];
};

// The processes' physical footprint, what Activity Monitor's Memory column
// shows, through `file`. Windows has no such measure, and its numbers are a
// real PC's (U6).
export const footprintsOf = bySystem({
	mac: async (pids: number[], file: string): Promise<Footprints> => {
		await Bun.$`footprint --json ${file} ${pids.flatMap((pid) => ["-p", String(pid)])}`
			.quiet()
			.nothrow();

		return JSON.parse(readFileSync(file, "utf8")) as Footprints;
	},
	windows: async (_pids: number[], _file: string): Promise<Footprints> => {
		throw noWindowsSide(
			"the processes' footprint",
			"U6",
			"a real PC's numbers",
		);
	},
});
