// Sends one command to a held test copy's automation channel, for a pass's
// setup (qa-hold.ts): bun tests/rigs/qa-send.ts <qa-session.json> <command> [json].
import { heldApp } from "./front";

const [sessionFile, command, args] = Bun.argv.slice(2);

if (!sessionFile || !command) {
	throw new Error("usage: qa-send.ts <qa-session.json> <command> [json]");
}

const { result } = await heldApp(sessionFile).send(
	command,
	JSON.parse(args ?? "{}"),
);

console.log(JSON.stringify({ ok: true, result }));
