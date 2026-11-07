// What a pass by hand does to a held test copy (qa-hold.ts) in front of the
// screen, under docs/building.md's rules for taking it: before each key, the
// test copy is proved to be the frontmost app and nobody else is proved to have
// touched the Mac. It stops at the first sign of the user, with exit code 3.
//
//   bun tests/rigs/front.ts guard [idle seconds, 600]
//       exit 0 only with the Mac that idle and its screen unlocked.
//   bun tests/rigs/front.ts <qa-session.json> <step>...
//       front  key:<key>  hot:<mod+mod+key>  text:<text>  menu:<Item>...<Item>
//       sleep:<seconds>  shot:<file>  send:<command>[:<json>]
import { readFileSync } from "node:fs";
import { dirname } from "node:path";
import type { Browser } from "../lib/browser";
import { call, isScreenLocked, mainWindowOf } from "../lib/cua";
import { PaneApp } from "../lib/pane";

const stop = (why: string): never => {
	console.error(`STOP: ${why}`);
	process.exit(3);
};

async function idleSeconds(): Promise<number> {
	const nanoseconds = /"HIDIdleTime" = (\d+)/.exec(
		await Bun.$`ioreg -c IOHIDSystem`.quiet().text(),
	)?.[1];

	return Number(nanoseconds) / 1e9;
}

async function frontmostPid(): Promise<number> {
	const front = (await Bun.$`lsappinfo front`.quiet().text()).trim();

	const pid = /\bpid"?\s*=\s*(\d+)/.exec(
		await Bun.$`lsappinfo info -only pid ${front}`.quiet().text(),
	)?.[1];

	return Number(pid);
}

export type Session = { pid: number; dataDir: string; automation: string };

export const readSession = (file: string) =>
	JSON.parse(readFileSync(file, "utf8")) as Session;

// The held copy's automation channel.
export function heldApp(file: string): PaneApp {
	const { pid, dataDir, automation } = readSession(file);

	return new PaneApp({ pid } as Browser, dataDir, automation, dirname(file));
}

// Runs steps on the held copy of `sessionFile`, proving the front before each
// input. Another app in front stops it, as does input that isn't its own: the
// Mac idle for less time than since its own last input.
export async function runSteps(sessionFile: string, steps: string[]) {
	const session = readSession(sessionFile);
	let lastInput = Date.now() - 1_000;

	const guard = async () => {
		const sinceOwn = (Date.now() - lastInput) / 1_000;
		const idle = await idleSeconds();

		if (idle < sinceOwn - 1.5) {
			stop(`input from someone else (idle ${idle} s, own ${sinceOwn} s ago)`);
		}

		const front = await frontmostPid();

		if (front !== session.pid) {
			stop(`pid ${front} is in front, not the test copy's ${session.pid}`);
		}
	};

	const input = async (tool: string, args: Record<string, unknown>) => {
		await guard();

		const reply = await call<Record<string, unknown>>(tool, {
			pid: session.pid,
			window_id: await mainWindowOf(session.pid),
			delivery_mode: "foreground",
			...args,
		});

		lastInput = Date.now();
		console.log(tool, JSON.stringify(args), reply.effect ?? reply.status ?? "");
	};

	for (const step of steps) {
		const colon = step.indexOf(":");
		const kind = colon < 0 ? step : step.slice(0, colon);
		const argument = colon < 0 ? "" : step.slice(colon + 1);

		if (kind === "front") {
			await call("bring_to_front", {
				pid: session.pid,
				window_id: await mainWindowOf(session.pid),
			});

			lastInput = Date.now();
		} else if (kind === "key") {
			await input("press_key", { key: argument });
		} else if (kind === "hot") {
			await input("hotkey", { keys: argument.split("+") });
		} else if (kind === "text") {
			await input("type_text", { text: argument });
		} else if (kind === "menu") {
			await guard();

			await call("invoke_menu", {
				pid: session.pid,
				path: argument.split(">"),
			});

			lastInput = Date.now();
		} else if (kind === "sleep") {
			await Bun.sleep(Number(argument) * 1_000);
		} else if (kind === "shot") {
			// A capture of the test copy's own window, never the screen.
			const window = await mainWindowOf(session.pid);
			await Bun.$`/usr/sbin/screencapture -x -o -l ${window} ${argument}`.quiet();
		} else if (kind === "send") {
			const [command = "", json = "{}"] = argument.split(/:(.*)/s);

			await heldApp(sessionFile).send(command, JSON.parse(json));
		} else {
			throw new Error(`unknown step ${step}`);
		}
	}
}

if (import.meta.main) {
	const [first, ...rest] = Bun.argv.slice(2);

	if (first === "guard") {
		const idle = await idleSeconds();
		const locked = await isScreenLocked();

		console.log(JSON.stringify({ idle, locked }));

		if (locked || idle < Number(rest[0] ?? 600)) {
			process.exit(3);
		}
	} else if (first) {
		await runSteps(first, rest);
	} else {
		throw new Error(
			"usage: front.ts guard [idle seconds] | <session> <step>...",
		);
	}
}
