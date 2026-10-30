// Each shortcut the probe gets is kept for the scenario, in session storage.
chrome.commands.onCommand.addListener(async (name, tab) => {
	const { commands = [] } = await chrome.storage.session.get("commands");

	commands.push({ name, tabId: tab?.id ?? null });

	await chrome.storage.session.set({ commands });
});
