// The service worker of fingerprint.html: answers with what it sees, and the
// request headers of a fetch it makes itself.
importScripts("fingerprint.js");

self.addEventListener("install", () => self.skipWaiting());

self.addEventListener("activate", (event) =>
	event.waitUntil(self.clients.claim()),
);

self.addEventListener("message", (event) => {
	const [port] = event.ports;

	event.waitUntil(
		(async () => {
			const fingerprint = await self.collectFingerprint();

			const headers = await (
				await fetch("headers?from=service-worker", { cache: "no-store" })
			).json();

			port.postMessage({ fingerprint, headers });
		})(),
	);
});
