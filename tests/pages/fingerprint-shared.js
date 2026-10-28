// A shared worker of fingerprint.html: answers each port with what it sees.
importScripts("fingerprint.js");

self.onconnect = (event) => {
	const [port] = event.ports;

	port.onmessage = async () => {
		port.postMessage(await self.collectFingerprint());
	};
};
