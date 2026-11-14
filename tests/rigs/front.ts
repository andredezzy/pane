// What a pass by hand does to a held test copy (qa-hold.ts) in front of the
// screen, under docs/building.md's rules for taking it: before each key, the
// test copy is proved to be the frontmost app and nobody else is proved to have
// touched the Mac. It stops at the first sign of the user, with exit code 3.
//
//   bun tests/rigs/front.ts guard [idle seconds, 600]
//       exit 0 only with the Mac that idle and its screen unlocked.
//   bun tests/rigs/front.ts <qa-session.json> <step>...
//       front  key:<key>  hot:<mod+mod+key>  text:<text>  menu:<Item>...<Item>
//       click:<x>,<y>  rclick:<x>,<y>  drag:<x>,<y>,<x>,<y>  sleep:<seconds>
//       shot:<file>
//       send:<command>[:<json>]
//   The input is Aside Computer Use's (lib/computer-use.ts): `front` takes the
//   front with real input, holding the user's mouse and keyboard until the
//   steps end; keys are X's names or the lower-case ones (`escape`,
//   `cmd+shift+t`); coordinates are points in the copy's main window, from its
//   top left, as Aside's screenshot of it shows them.
import { readFileSync } from "node:fs";
import { dirname } from "node:path";
import type { Browser } from "../lib/browser";
import {
	clickAt,
	dragFrom,
	invokeMenu,
	isScreenLocked,
	leaveFront,
	mainWindowOf,
	pressKey,
	takeFront,
	typeText,
} from "../lib/computer-use";
import { PaneApp } from "../lib/pane";
import { captureWindow, frontmostPid, idleSeconds } from "../lib/tools";

const stop = (why: string): never => {
	console.error(`STOP: ${why}`);
	process.exit(3);
};

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

	// Each input once the front is proved, to the copy alone.
	const input = async (what: string, act: () => Promise<void>) => {
		await guard();
		await act();

		lastInput = Date.now();
		console.log(what);
	};

	const { pid } = session;
	let front = false;

	try {
		for (const step of steps) {
			const colon = step.indexOf(":");
			const kind = colon < 0 ? step : step.slice(0, colon);
			const argument = colon < 0 ? "" : step.slice(colon + 1);

			if (kind === "front") {
				await takeFront(pid, "a pass by hand on Pane's test copy");
				front = true;
				lastInput = Date.now();
			} else if (kind === "key" || kind === "hot") {
				await input(step, () => pressKey(pid, argument));
			} else if (kind === "text") {
				await input("text", () => typeText(pid, argument));
			} else if (kind === "click" || kind === "rclick") {
				const [x = 0, y = 0] = argument.split(",").map(Number);
				const button = kind === "click" ? "left" : "right";
				await input(step, () => clickAt(pid, x, y, button));
			} else if (kind === "drag") {
				const [fromX = 0, fromY = 0, toX = 0, toY = 0] = argument
					.split(",")
					.map(Number);

				await input(step, () => dragFrom(pid, [fromX, fromY], [toX, toY]));
			} else if (kind === "menu") {
				await input(step, () => invokeMenu(pid, argument.split(">")));
			} else if (kind === "sleep") {
				await Bun.sleep(Number(argument) * 1_000);
			} else if (kind === "shot") {
				// A capture of the test copy's own window, never the screen.
				const window = await mainWindowOf(pid);

				if (!(await captureWindow(window, argument))) {
					throw new Error(`the window ${window} was not captured`);
				}
			} else if (kind === "send") {
				const [command = "", json = "{}"] = argument.split(/:(.*)/s);

				await heldApp(sessionFile).send(command, JSON.parse(json));
			} else {
				throw new Error(`unknown step ${step}`);
			}
		}
	} finally {
		// The user's mouse and keyboard back, whatever stopped the steps.
		if (front) {
			await leaveFront(pid);
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
