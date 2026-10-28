// Answers the popup and the scenario: the active tab of the current window,
// and opening tabs and windows.
chrome.runtime.onMessage.addListener((message, _sender, reply) => {
	if (message.ask === "active") {
		chrome.tabs.query({ active: true, currentWindow: true }).then(([tab]) => {
			reply(tab ? { id: tab.id, url: tab.url, windowId: tab.windowId } : null);
		});

		return true;
	}

	if (message.ask === "createTab") {
		chrome.tabs
			.create({ url: message.url })
			.then((tab) => reply({ id: tab.id, windowId: tab.windowId }));

		return true;
	}

	if (message.ask === "createWindow") {
		chrome.windows
			.create({ url: message.url, type: message.type ?? "normal" })
			.then((window) => reply({ id: window.id }));

		return true;
	}

	return false;
});
