// The popup's page: its field takes the keys, and Close closes it as a page
// closes itself (window.close()).
window.result = { opened: Date.now() };

document
	.getElementById("close")
	.addEventListener("click", () => window.close());
