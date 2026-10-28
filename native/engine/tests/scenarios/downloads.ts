import { readFileSync } from "node:fs";
import { join } from "node:path";
import { launchPane, tabOf } from "../lib/pane";
import { expect, type Scenario } from "../run";

const FIXTURE = join(import.meta.dir, "..", "pages", "pane-download.txt");

// A link to an attachment downloads into the profile's download directory,
// without a dialog, and reports progress and completion; the tab stays put.
const downloads: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Downloads");
	const url = context.fixtures.url("downloads.html");
	const tab = await app.openTab(profile, url);
	const page = await app.page(tab, url);

	const directory = context.tempDir("pane-downloads-");
	await app.send("setDownloadDirectory", { profile, path: directory });

	await page.evaluate("document.getElementById('file').click()");

	const done = await app.waitFor("a completed download", (state) =>
		state.downloads.some((download) => download.state === "complete"),
	);

	const [download] = done.downloads;
	expect(download, "no download reported");

	expect(
		download.path?.startsWith(directory),
		`saved to ${download.path}, not ${directory}`,
	);

	const expected = readFileSync(FIXTURE, "utf8");
	const saved = readFileSync(download.path as string, "utf8");
	expect(saved === expected, "the saved file differs from the fixture");

	expect(
		download.receivedBytes === expected.length,
		`received ${download.receivedBytes} bytes`,
	);

	expect(
		tabOf(done, tab)?.url === url,
		"the tab navigated away for a download",
	);

	return { download, directory, tab: tabOf(done, tab) };
};

export default downloads;
