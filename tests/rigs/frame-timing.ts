// Speed as felt: records the held test copy's own window while steps run, and
// prints when its picture changed, in ms into the recording, with the steps
// starting about a second in. ffmpeg reads the recording (brew install ffmpeg);
// the same steps on two builds compare.
//
//   bun tests/rigs/frame-timing.ts <qa-session.json> <out.mov> <seconds> <step>...
//
// Steps as in front.ts: ⌃⌘L is `hot:ctrl+cmd+l`, a reload `hot:cmd+r`.
import { mainWindowOf } from "../lib/cua";
import { readSession, runSteps } from "./front";

// A frame counts as changed past this mean difference of 0 to 255, in grey at
// 320 wide: a clock or a caret stays under it, a page or a panel doesn't.
const CHANGED = 0.05;
const LEAD_IN_MS = 1_000;

// The times, in ms into the recording, of the frames that changed.
async function changedFrames(movie: string): Promise<number[]> {
	const filter =
		"fps=60,scale=320:-1,format=gray,tblend=all_mode=difference,signalstats,metadata=print:file=-";

	const output =
		await Bun.$`ffmpeg -hide_banner -i ${movie} -vf ${filter} -f null -`
			.quiet()
			.text();

	const times: number[] = [];
	let at = 0;

	for (const line of output.split("\n")) {
		const time = /pts_time:([\d.]+)/.exec(line)?.[1];

		if (time) {
			at = Number(time) * 1_000;
		} else if (
			line.startsWith("lavfi.signalstats.YAVG=") &&
			Number(line.split("=")[1]) > CHANGED
		) {
			times.push(Math.round(at));
		}
	}

	return times;
}

const [sessionFile, movie, seconds, ...steps] = Bun.argv.slice(2);

if (!sessionFile || !movie || !seconds || steps.length === 0) {
	throw new Error(
		"usage: frame-timing.ts <qa-session.json> <out.mov> <seconds> <step>...",
	);
}

const window = await mainWindowOf(readSession(sessionFile).pid);

const recording = Bun.spawn(
	["/usr/sbin/screencapture", "-x", "-v", "-V", seconds, `-l${window}`, movie],
	{ stdout: "ignore", stderr: "ignore" },
);

await Bun.sleep(LEAD_IN_MS);
await runSteps(sessionFile, steps);
await recording.exited;

// The recording's own start shows as a change in its first frames.
const changed = (await changedFrames(movie)).filter(
	(time) => time >= LEAD_IN_MS / 2,
);

console.log(
	JSON.stringify({
		first: changed[0],
		last: changed.at(-1),
		changed: changed.length,
	}),
);
