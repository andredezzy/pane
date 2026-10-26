// A dedicated worker of fingerprint.html: answers with what it sees.
importScripts("fingerprint.js");

self.onmessage = async () => {
	self.postMessage(await self.collectFingerprint());
};
