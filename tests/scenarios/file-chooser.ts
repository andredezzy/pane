import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { canDriveUI } from "../lib/cua";
import { pageResult, WindowWatcher, waitForSheet } from "../lib/surfaces";
import { clickAt, launchTraced } from "../lib/traced";
import { expect, type Scenario } from "../run";

// <input type=file> and showSaveFilePicker() open the app's open and save
// panels on the window; the files chosen reach the page, and Cancel reaches
// it as a cancel.
const fileChooser: Scenario = async (context) => {
	const { app, has } = await launchTraced(context, "file-chooser");
	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Files");
	const url = context.fixtures.url("files.html");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);
	const evaluate = (expression: string) => page.evaluate(expression);
	const folder = context.tempDir("pane-files-");
	const chosen = join(folder, "chosen.txt");
	writeFileSync(chosen, "Chosen through the Pane panel\n");

	// The open panel. On a locked screen NSOpenPanel takes no programmatic
	// selection, so the chooser is answered beside it.
	const box = await page.evaluate<{ x: number; y: number }>(
		"(() => { const r = document.getElementById('file').getBoundingClientRect(); return { x: r.left + 10, y: r.top + 5 }; })()",
	);

	await clickAt((method, params) => page.send(method, params), box.x, box.y);
	const openPanel = await waitForSheet(app, "fileChooser");
	await watcher.sample("open panel");
	expect(openPanel.panel === "open", `panel ${openPanel.panel}`);
	await app.send("answerFileChooser", { paths: [chosen] });
	const opened = await pageResult<{ name: string; text: string }>(evaluate);

	// Cancel, answered beside the panel as the choices are: the panels run
	// out of process, where neither accessibility (the app's tree has no
	// Cancel) nor a key posted to the app (it goes to the main window)
	// reaches. It cancels the chooser as the panel's Cancel does. The page
	// hears a cancel, and keeps its file.
	const cancel = async () => {
		await waitForSheet(app, "fileChooser");
		await app.send("answerFileChooser", { cancel: true });
	};

	await page.evaluate("window.result = undefined");
	await clickAt((method, params) => page.send(method, params), box.x, box.y);
	await cancel();
	const openCancelled = await pageResult<string>(evaluate);

	const kept = await page.evaluate<string | null>(
		"document.getElementById('file').files[0]?.name ?? null",
	);

	await page.send("Runtime.evaluate", {
		expression: "saveFile()",
		userGesture: true,
	});

	await cancel();
	const saveCancelled = await pageResult<{ error?: string }>(evaluate);

	expect(openCancelled === "cancelled", `open cancelled: ${openCancelled}`);
	expect(kept === "chosen.txt", `the input lost its file: ${kept}`);

	expect(
		saveCancelled.error === "AbortError",
		`save cancelled: ${JSON.stringify(saveCancelled)}`,
	);

	// The save panel, answered through its own name field and Save.
	const target = join(folder, "pane-saved.txt");

	await page.send("Runtime.evaluate", {
		expression: "saveFile()",
		userGesture: true,
	});

	const savePanel = await waitForSheet(app, "fileChooser");
	expect(savePanel.panel === "save", `panel ${savePanel.panel}`);
	await app.send("answerFileChooser", { paths: [target] });
	const saved = await pageResult<{ saved?: string; error?: string }>(evaluate);
	const contents = readFileSync(target, "utf8");

	expect(
		opened.name === "chosen.txt" && opened.text.startsWith("Chosen"),
		`page read ${JSON.stringify(opened)}`,
	);

	expect(
		saved.saved === "pane-saved.txt",
		`save gave ${JSON.stringify(saved)}`,
	);

	expect(contents.startsWith("Saved by the Pane"), "the saved file is wrong");

	expect(
		has(
			"PaneBrowserWindowDelegate.runFileChooser",
			(args) => args.mode === "open",
		),
		"trace lacks the open chooser",
	);

	expect(
		has(
			"PaneBrowserWindowDelegate.runFileChooser",
			(args) => args.mode === "save",
		),
		"trace lacks the save chooser",
	);

	expect(has("PaneFileChooser.cancel"), "trace lacks the cancels");

	const windows = await watcher.stop();

	expect(
		windows.others.length === 0,
		`Chrome windows appeared: ${JSON.stringify(windows.others)}`,
	);

	return {
		openPanel,
		savePanel,
		opened,
		cancelled: {
			open: { result: openCancelled, kept },
			save: saveCancelled,
		},
		saved,
		answeredVia: !(await canDriveUI())
			? "automation (no accessibility)"
			: "automation",
		windows,
	};
};

export default fileChooser;
