import { existsSync, mkdirSync, readdirSync } from "node:fs";
import { join } from "node:path";
import { homeEnvironment } from "../lib/browser";
import { bySystem } from "../lib/platform";
import { expect, type Scenario } from "../run";

// A launch Pane refuses ends with 1, showing nothing in a scenario's run,
// before anything is written. On both systems the runner's own refusal: a
// test copy without the mock keychain (pane/app/test_copy.h, ADR 0012). On
// Windows also a user's: a launch whose engine would keep its data outside
// the data folder's engine folder (a policy's UserDataDir, another program's
// --user-data-dir, ADR 0121), which says why in Windows' own MessageBox
// (Paper 11.16) and, in a scenario's run, where the automation channel's
// folder is set, says it only in the log, so no test copy waits on it.
const REFUSES_ANOTHER_FOLDER = bySystem({ mac: false, windows: true });

const REFUSED_LINE =
	"Pane refused a launch whose engine would keep its data outside the data folder's engine folder";

type Ended = { exited: boolean; code: number | null; log: string };

const refusedLaunch: Scenario = async (context) => {
	const root = context.tempDir("pane-refused-");

	// A launch of the test build in a home and data of its own: whether it
	// ended within `wait` ms, with what, and what it logged.
	const launch = async (
		name: string,
		args: string[],
		env: Record<string, string>,
		wait = 30_000,
	): Promise<Ended> => {
		const home = join(root, `${name}-home`);
		mkdirSync(home, { recursive: true });

		const launched = Bun.spawn(
			[
				context.binary,
				"--no-first-run",
				"--no-default-browser-check",
				"--enable-logging=stderr",
				...args,
			],
			{
				env: { ...process.env, ...homeEnvironment(home), ...env },
				stdout: "ignore",
				stderr: "pipe",
			},
		);

		const exited = await Promise.race([
			launched.exited.then(() => true),
			Bun.sleep(wait).then(() => false),
		]);

		if (!exited) {
			launched.kill("SIGKILL");
			await launched.exited;
		}

		return {
			exited,
			code: launched.exitCode,
			log: await new Response(launched.stderr).text(),
		};
	};

	const isEmpty = (folder: string) =>
		!existsSync(folder) || readdirSync(folder).length === 0;

	// The runner's refusal: a test copy's data with no mock keychain.
	const noMockData = join(root, "no-mock-data");
	const noMock = await launch("no-mock", [], { PANE_DATA_DIR: noMockData });

	expect(
		noMock.exited && noMock.code === 1 && isEmpty(noMockData),
		`a test copy without the mock keychain: ${JSON.stringify({ ...noMock, log: undefined })}`,
	);

	if (!REFUSES_ANOTHER_FOLDER) {
		return;
	}

	// The user's: an engine folder the command line names elsewhere. In a
	// scenario's run, where the channel's folder is set, the refusal shows no
	// MessageBox, so the launch ends at once; its line is in the log.
	const data = join(root, "data");
	const elsewhere = join(root, "elsewhere");
	const automation = join(root, "automation");
	mkdirSync(automation, { recursive: true });

	const refused = await launch(
		"elsewhere",
		["--use-mock-keychain", `--user-data-dir=${elsewhere}`],
		{ PANE_DATA_DIR: data, PANE_AUTOMATION_DIR: automation },
	);

	expect(
		refused.exited &&
			refused.code === 1 &&
			refused.log.includes(REFUSED_LINE) &&
			isEmpty(data),
		`a launch given another folder: ${JSON.stringify({
			exited: refused.exited,
			code: refused.code,
			logged: refused.log.includes(REFUSED_LINE),
			dataWritten: !isEmpty(data),
		})}`,
	);
};

export default refusedLaunch;
