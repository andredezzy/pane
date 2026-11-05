import { launchPane } from "../lib/pane";
import { swiftTool } from "../lib/tools";
import { expect, type Scenario } from "../run";

// What a process of the same user reaches of Pane's processes' memory, where the
// keys are (ADR 0121). The browser process, the network service, and the Storage
// Service each refuse lldb and a program of the user's own (no task port, control
// or read). Apple's inspection tools, which carry com.apple.system-task-ports.read,
// do run: they show regions, class names, and stacks, and a value planted in the
// process is in none of what they print.
type Ran = { code: number; output: string };

async function run(command: string[]): Promise<Ran> {
	const child = Bun.spawn(command, {
		stdout: "pipe",
		stderr: "pipe",
		stdin: "ignore",
	});

	const timer = setTimeout(() => child.kill("SIGKILL"), 60_000);

	const [out, error] = await Promise.all([
		new Response(child.stdout).text(),
		new Response(child.stderr).text(),
	]);

	clearTimeout(timer);

	return { code: await child.exited, output: `${out}\n${error}` };
}

const memoryGuarded: Scenario = async (context) => {
	const planted = `pane-memory-${crypto.randomUUID()}`;
	const app = await launchPane(context);
	const profile = await app.createProfile(planted);
	const url = context.fixtures.url("smoke.html?memory");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);

	await page.evaluate(
		`localStorage.setItem("memory", ${JSON.stringify(planted)}); document.cookie = "memory=${planted}; max-age=9000"`,
	);

	const rows = (await Bun.$`ps -axo pid=,args=`.text())
		.split("\n")
		.filter((row) => row.includes(app.dataDir.replace(/^\/private/, "")));

	const pidOf = (kind: string) =>
		Number(
			rows
				.find((row) => row.includes(kind))
				?.trim()
				.split(/\s+/)[0],
		);

	const processes = {
		browser: app.browser.pid,
		network: pidOf("network.mojom.NetworkService"),
		storage: pidOf("storage.mojom.StorageService"),
	};

	expect(
		processes.network > 0 && processes.storage > 0,
		`no helpers found: ${JSON.stringify(processes)}`,
	);

	const probe = await swiftTool("task-probe");
	const results: Record<string, unknown> = {};

	for (const [name, pid] of Object.entries(processes)) {
		const lldb = await run([
			"/usr/bin/lldb",
			"--batch",
			"--no-lldbinit",
			"-o",
			`process attach --pid ${pid}`,
			"-o",
			"process detach",
		]);

		expect(
			/attach failed/.test(lldb.output) &&
				!/Process \d+ stopped/.test(lldb.output),
			`lldb attached to the ${name} process`,
		);

		const task = JSON.parse((await run([probe, String(pid)])).output) as {
			taskForPid: number;
			taskReadForPid: number;
			control: number;
			read: number;
		};

		expect(
			task.taskForPid !== 0 &&
				task.taskReadForPid !== 0 &&
				task.control === 0 &&
				task.read === 0,
			`a program of the user's own got a task port of the ${name} process: ${JSON.stringify(task)}`,
		);

		const tools: Record<string, string[]> = {
			vmmap: ["/usr/bin/vmmap", String(pid)],
			heap: ["/usr/bin/heap", "-addresses=all", String(pid)],
			stringdups: ["/usr/bin/stringdups", "-minimumCount", "1", String(pid)],
			leaks: ["/usr/bin/leaks", "--fullContent", String(pid)],
		};

		const printed: Record<string, number> = {};

		for (const [tool, command] of Object.entries(tools)) {
			const { output } = await run(command);
			printed[tool] = output.length;

			expect(
				!output.includes(planted),
				`${tool} printed a value planted in the ${name} process`,
			);
		}

		results[name] = { lldb: "refused", task, printed };
	}

	await app.quit();

	return results;
};

export default memoryGuarded;
