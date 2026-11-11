import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { launchPane } from "../lib/pane";
import { pageResult, WindowWatcher, waitForSheet } from "../lib/surfaces";
import { clickAt } from "../lib/traced";
import { expect, type Scenario } from "../run";

// <input type=file> and showSaveFilePicker() open the app's open and save
// panels on the window; the files chosen reach the page, and Cancel reaches
// it as a cancel.
const fileChooser: Scenario = async (context) => {
	// In the background, the user's windows cover most of the test window,
	// and the system's panel, out of process, covers the rest: macOS reports
	// it occluded, Chromium hides the page and doesn't show it again once the
	// panel goes, and Chrome's FileSelectHelper ends the next chooser of a
	// page that isn't visible. As on a locked screen (ADR 0012), the page is
	// kept from occlusion's backgrounding; the panels are this scenario's
	// subject, not occlusion.
	const app = await launchPane(context, {
		args: ["--disable-backgrounding-occluded-windows"],
	});

	const watcher = new WindowWatcher(app).start();
	const profile = await app.createProfile("Files");
	const url = context.fixtures.url("files.html");
	const { page } = await app.openPage(profile, url);
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
	await watcher.sample();
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

	await page.gesture("saveFile()");

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

	await page.gesture("saveFile()");

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

	await watcher.stop();
};

export default fileChooser;
