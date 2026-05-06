import { useEffect, useRef } from "react";

import { HotkeyEvent } from "../../constants/hotkey-event";
import { trpc } from "../trpc";

export { HotkeyEvent };

export function useHotkeyEvents(handler: (event: HotkeyEvent) => void): void {
	const handlerRef = useRef(handler);
	handlerRef.current = handler;

	useEffect(() => {
		const subscription = trpc.hotkeys.events.subscribe(undefined, {
			onData(event: string) {
				handlerRef.current(event as HotkeyEvent);
			},
			onError(error) {
				console.error("[hotkeys] subscription error:", error);
			},
		});

		return () => subscription.unsubscribe();
	}, []);
}
