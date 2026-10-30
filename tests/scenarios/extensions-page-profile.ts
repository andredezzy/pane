import { join } from "node:path";
import {
	canDriveUI,
	clickControl,
	mainWindowOf,
	windowState,
} from "../lib/cua";
import { launchPane } from "../lib/pane";
import { expect, type Scenario } from "../run";

// The Extensions page's profile select: choosing another profile shows that
// profile's extensions, and deleting the chosen profile brings the page back
// to the active one, with the select still there. The select is chosen in
// with real presses through accessibility, so it needs an unlocked screen.
const extensionsPageProfile: Scenario = async (context) => {
	if (!(await canDriveUI())) {
		return { skipped: "the select is pressed through accessibility" };
	}

	const app = await launchPane(context);
	const alpha = await app.createProfile("Alpha");
	const bravo = await app.createProfile("Bravo", "ROSE");
	const url = context.fixtures.url("light.html?extensions-page-profile");
	const tab = await app.openTab(alpha, url);
	await app.page(tab, url);

	await app.send("extensionsMenu", { control: "manage" });

	await app.waitFor(
		"the Extensions page",
		(state) => state.page === "extensions",
	);

	const pid = app.browser.pid;
	const window = await mainWindowOf(pid);

	// What the page shows: the empty state names its profile, and the
	// select is there or not.
	const page = async (name: string) => {
		const tree = (
			await windowState(pid, window, join(context.out, `${name}.png`))
		).tree_markdown;

		return {
			shows: tree.match(/"No extensions in ([^"]+)"/)?.[1] ?? null,
			select: tree.includes("id=pane.extensions.profile"),
		};
	};

	const atStart = await page("extensions-page-profile-start");

	expect(
		atStart.shows === "Alpha" && atStart.select,
		`at the start: ${JSON.stringify(atStart)}`,
	);

	// The menu closes as the app gives up being frontmost (another app's
	// window a moment), so one that shut before its row was read is opened
	// again.
	for (let attempt = 1; ; attempt++) {
		await clickControl(pid, window, { title: "Profile", role: /AXMenuButton/ });

		try {
			await clickControl(pid, "any", { title: "Bravo", role: /AXMenuItem/ });

			break;
		} catch (error) {
			if (attempt === 3 || !String(error).includes('no control "Bravo"')) {
				throw error;
			}
		}
	}

	await Bun.sleep(300);

	const chosen = await page("extensions-page-profile-chosen");

	expect(
		chosen.shows === "Bravo" && chosen.select,
		`Bravo chosen: ${JSON.stringify(chosen)}`,
	);

	await app.send("deleteProfile", { profile: bravo });

	await app.waitFor(
		"Bravo deleted",
		(state) => !state.profiles.some((profile) => profile.id === bravo),
	);

	await Bun.sleep(300);

	const afterDelete = await page("extensions-page-profile-deleted");

	expect(
		afterDelete.shows === "Alpha" && afterDelete.select,
		`the deleted profile stayed chosen: ${JSON.stringify(afterDelete)}`,
	);

	return { atStart, chosen, afterDelete };
};

export default extensionsPageProfile;
