import { launchPane, tabOf } from "../lib/pane";
import { settledWindowsOf } from "../lib/windows";
import { expect, type Scenario } from "../run";

// Pages that open windows get tabs instead, in their own profile, right after
// the opener; a page that closes itself takes its tab with it.
const popupNewTab: Scenario = async (context) => {
	const app = await launchPane(context);
	const profile = await app.createProfile("Popups");
	const other = await app.createProfile("Other");

	await app.openTab(
		other,
		context.fixtures.url("isolation.html?profile=other"),
	);

	const url = context.fixtures.url("popup.html");
	const opener = await app.openTab(profile, url);
	const page = await app.page(opener, url);
	const viaLink = context.fixtures.url("popup-target.html?via=link");
	const viaOpen = context.fixtures.url("popup-target.html?via=open");

	await page.send("Runtime.evaluate", {
		expression: "document.getElementById('blank').click()",
		userGesture: true,
	});

	const afterLink = await app.waitFor("the link's tab", (state) =>
		state.profiles.some((p) => p.tabs.some((tab) => tab.url === viaLink)),
	);

	await app.send("select", { tab: opener });

	await page.send("Runtime.evaluate", {
		expression: "openPopup()",
		userGesture: true,
	});

	const afterOpen = await app.waitFor("the popup's tab", (state) =>
		state.profiles.some((p) =>
			p.tabs.some((tab) => tab.url === viaOpen && !tab.loading),
		),
	);

	const tabs = afterOpen.profiles.find((p) => p.id === profile)?.tabs ?? [];
	const popupTab = tabs.find((tab) => tab.url === viaOpen);

	expect(
		tabs.map((tab) => tab.url).join(" ") === [url, viaOpen, viaLink].join(" "),
		`tab order: ${tabs.map((tab) => tab.url).join(", ")}`,
	);

	expect(
		afterOpen.activeTabID === popupTab?.id,
		"the popup's tab is not selected",
	);

	expect(
		(afterOpen.profiles.find((p) => p.id === other)?.tabs.length ?? 0) === 1,
		"a popup landed in the other profile",
	);

	const windows = await settledWindowsOf(app.browser.pid, 1);

	expect(
		windows.count === 1,
		`expected 1 window, got ${JSON.stringify(windows.windows)}`,
	);

	const popupPage = await app.page(popupTab?.id as string, viaOpen);
	await popupPage.evaluate("window.close()").catch(() => {});

	const afterClose = await app.waitFor(
		"the popup's tab to close",
		(state) =>
			!state.profiles.some((p) => p.tabs.some((tab) => tab.url === viaOpen)),
	);

	expect(tabOf(afterClose, opener), "the opener's tab went away");

	return {
		afterLink: afterLink.profiles,
		afterOpen: afterOpen.profiles,
		afterClose: afterClose.profiles,
		windows,
	};
};

export default popupNewTab;
