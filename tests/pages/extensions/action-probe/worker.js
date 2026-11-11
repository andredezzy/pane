// The probe's worker: the scenarios set its badge and read what its popup
// did through it.
chrome.runtime.onMessage.addListener((message, _sender, reply) => {
	if (message.ask === "ping") {
		reply({ ok: true });
	}
});
