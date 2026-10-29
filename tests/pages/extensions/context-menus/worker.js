// Two items for a page's menu, which Chrome puts in a submenu under the
// extension's name, and two for its action's menu. Each click is kept for
// the scenario, in session storage.
chrome.runtime.onInstalled.addListener(() => {
	chrome.contextMenus.create({
		id: "page-item",
		title: "Probe page item",
		contexts: ["page"],
	});

	chrome.contextMenus.create({
		id: "page-check",
		title: "Probe page check",
		type: "checkbox",
		contexts: ["page"],
	});

	chrome.contextMenus.create({
		id: "action-item",
		title: "Probe action item",
		contexts: ["action"],
	});

	chrome.contextMenus.create({
		id: "action-check",
		title: "Probe action check",
		type: "checkbox",
		checked: true,
		contexts: ["action"],
	});
});

chrome.contextMenus.onClicked.addListener(async (info, tab) => {
	const { clicks = [] } = await chrome.storage.session.get("clicks");

	clicks.push({
		id: info.menuItemId,
		checked: info.checked ?? null,
		pageUrl: info.pageUrl ?? null,
		tabId: tab?.id ?? null,
	});

	await chrome.storage.session.set({ clicks });
});
