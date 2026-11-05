// Sends one command to a held test copy's automation channel, for a pass's setup
// (tests/scenarios/qa-hold.ts): bun tests/tools/qa-send.ts <qa-session.json> <command> [json].
import {
	existsSync,
	readFileSync,
	renameSync,
	rmSync,
	writeFileSync,
} from "node:fs";
import { join } from "node:path";

const [sessionFile, command, args] = Bun.argv.slice(2);

if (!sessionFile || !command) {
	throw new Error("usage: qa-send.ts <qa-session.json> <command> [json]");
}

const { automation } = JSON.parse(readFileSync(sessionFile, "utf8")) as {
	automation: string;
};

const name = `${String(Date.now()).padStart(15, "0")}-qa.json`;
const temporary = join(automation, `${name}.tmp`);

writeFileSync(
	temporary,
	JSON.stringify({ command, ...JSON.parse(args ?? "{}") }),
);

renameSync(temporary, join(automation, "in", name));
const reply = join(automation, "out", name);

for (const until = Date.now() + 15_000; Date.now() < until; ) {
	if (existsSync(reply)) {
		const parsed = JSON.parse(readFileSync(reply, "utf8")) as {
			ok: boolean;
			result?: unknown;
			error?: string;
		};

		rmSync(reply, { force: true });

		console.log(
			JSON.stringify({
				ok: parsed.ok,
				result: parsed.result,
				error: parsed.error,
			}),
		);

		process.exit(parsed.ok ? 0 : 1);
	}

	await Bun.sleep(50);
}

throw new Error("no reply");
