// The popup's view of "the current window", and the worker's.
window.result = undefined;

(async () => {
	const [fromPopup] = await chrome.tabs.query({
		active: true,
		currentWindow: true,
	});

	const fromWorker = await chrome.runtime.sendMessage({ ask: "active" });

	window.result = {
		popup: fromPopup
			? { id: fromPopup.id, url: fromPopup.url, windowId: fromPopup.windowId }
			: null,
		worker: fromWorker,
	};

	document.getElementById("out").textContent = JSON.stringify(window.result);
})();
