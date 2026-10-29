// Each shortcut the probe gets is kept for the scenario, in session storage:
// its own command, and its action's (_execute_action, with no popup, fires
// action.onClicked).
const keep = async (name, tab) => {
	const { commands = [] } = await chrome.storage.session.get("commands");

	commands.push({ name, tabId: tab?.id ?? null });

	await chrome.storage.session.set({ commands });
};

chrome.commands.onCommand.addListener((name, tab) => keep(name, tab));

chrome.action.onClicked.addListener((tab) => keep("_execute_action", tab));
