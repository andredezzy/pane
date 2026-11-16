// notification.html's service worker: a notification's click is kept in the
// site's own storage, then opens the page again with ?clicked=<tag>, or
// focuses the open page whose address has the notification's `focus`.

self.addEventListener("install", () => self.skipWaiting());

self.addEventListener("activate", (event) => {
	event.waitUntil(self.clients.claim());
});

self.addEventListener("notificationclick", (event) => {
	const notification = event.notification;
	notification.close();

	event.waitUntil(
		(async () => {
			const cache = await caches.open("notification-clicks");

			await cache.put(
				`clicked/${encodeURIComponent(notification.tag)}`,
				new Response(
					JSON.stringify({
						tag: notification.tag,
						action: event.action,
						at: Date.now(),
					}),
				),
			);

			const focus = notification.data?.focus;

			if (focus) {
				const windows = await self.clients.matchAll({
					type: "window",
					includeUncontrolled: true,
				});

				const open = windows.find((client) => client.url.includes(focus));

				if (open) {
					await open.focus();

					return;
				}
			}

			await self.clients.openWindow(
				`notification.html?clicked=${encodeURIComponent(notification.tag)}`,
			);
		})(),
	);
});
