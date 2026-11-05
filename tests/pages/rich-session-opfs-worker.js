// The origin private file system through a sync access handle, which only a
// dedicated worker can open (tests/lib/rich-session.ts).
self.onmessage = async ({ data }) => {
	const root = await navigator.storage.getDirectory();

	const file = await root.getFileHandle("rich-session-sync.txt", {
		create: true,
	});

	const handle = await file.createSyncAccessHandle();
	const bytes = new TextEncoder().encode(data.value);
	handle.write(bytes, { at: 0 });
	handle.flush();
	const read = new Uint8Array(bytes.length);
	handle.read(read, { at: 0 });
	handle.close();
	self.postMessage(new TextDecoder().decode(read));
};
