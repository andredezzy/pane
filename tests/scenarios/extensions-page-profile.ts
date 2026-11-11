import { join } from "node:path";
import {
	EXTENSIONS_PAGE,
	extensionsPage,
	listed,
} from "../lib/chrome-extensions";
import { extensionId } from "../lib/extensions";
import { launchPane } from "../lib/pane";
import { poll } from "../lib/poll";
import { expect, type Scenario } from "../run";

const PROBE = join(
	import.meta.dir,
	"..",
	"pages",
	"extensions",
	"action-probe",
);

const NAME = "Pane action probe";

// Chrome's extensions page lists the extensions of the profile whose tab
// shows it (D3, Paper 10.1: no profile select, each profile's are in a tab
// of its own). The probe, on in Alpha and off in Bravo, reads on in Alpha's
// page and off in Bravo's, each opened by the extensions menu's Manage all
// extensions from that profile's page; deleting Bravo takes its page with
// its tabs, and Alpha's stays as it was.
const extensionsPageProfile: Scenario = async (context) => {
	const app = await launchPane(context, {
		args: [`--load-extension=${PROBE}`],
	});

	const alpha = await app.createProfile("Alpha");
	const bravo = await app.createProfile("Bravo", "ROSE");
	const alphaURL = context.fixtures.url("light.html?extensions-page-alpha");
	const bravoURL = context.fixtures.url("light.html?extensions-page-bravo");
	const { tab: alphaTab } = await app.openPage(alpha, alphaURL);
	const { tab: bravoTab } = await app.openPage(bravo, bravoURL);
	const id = await extensionId(app, alpha, NAME);
	await extensionId(app, bravo, NAME);

	await app.send("setExtensionEnabled", { id, profile: bravo, enabled: false });

	await app.waitFor("the probe off in Bravo", (state) =>
		(state.extensions[bravo] ?? []).some(
			(each) => each.id === id && !each.enabled,
		),
	);

	const open = async (profile: string, tab: string, near: string) => {
		await app.send("select", { tab });
		await app.send("extensionsMenu", { control: "puzzle" });
		await app.waitFor("the extensions menu", (state) => state.extensionsMenu);
		await app.send("extensionsMenu", { control: "manage" });

		return extensionsPage(app, profile, EXTENSIONS_PAGE, near);
	};

	const shows = (page: Awaited<ReturnType<typeof open>>["page"]) =>
		poll(
			"the probe on the page",
			async () => (await listed(page)).find((each) => each.id === id),
			10_000,
		);

	const alphaPage = await open(alpha, alphaTab, alphaURL);
	const bravoPage = await open(bravo, bravoTab, bravoURL);

	const inAlpha = await shows(alphaPage.page);
	const inBravo = await shows(bravoPage.page);

	expect(
		inAlpha.on && !inBravo.on && alphaPage.tab !== bravoPage.tab,
		`Alpha's page shows ${JSON.stringify(inAlpha)}, Bravo's ${JSON.stringify(inBravo)}`,
	);

	await app.send("deleteProfile", { profile: bravo });

	const afterDelete = await app.waitFor(
		"Bravo deleted",
		(state) => !state.profiles.some((profile) => profile.id === bravo),
	);

	expect(
		(afterDelete.profiles.find((each) => each.id === alpha)?.tabs ?? []).some(
			(each) => each.id === alphaPage.tab && each.url === EXTENSIONS_PAGE,
		),
		"Alpha's extensions page went with Bravo",
	);

	const still = await shows(alphaPage.page);

	expect(
		still.on,
		`Alpha's page shows ${JSON.stringify(still)} after Bravo went`,
	);
};

export default extensionsPageProfile;
