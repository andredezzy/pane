import { openPage } from "../lib/cdp";
import { windowsOf } from "../lib/windows";
import { expect, type Scenario } from "../run";

// The runner can drive the binary: open a fixture over CDP and read it back.
const smoke: Scenario = async ({ launch, fixtures }) => {
	const browser = await launch();
	const page = await openPage(browser.cdp, fixtures.url("smoke.html"));
	const title = await page.evaluate<string>("document.title");

	const marker = await page.evaluate<string>(
		"document.getElementById('marker').textContent",
	);

	expect(title === "Pane smoke", `title was ${JSON.stringify(title)}`);
	expect(marker === "smoke fixture", `marker was ${JSON.stringify(marker)}`);

	return { title, marker, windows: await windowsOf(browser.pid) };
};

export default smoke;
