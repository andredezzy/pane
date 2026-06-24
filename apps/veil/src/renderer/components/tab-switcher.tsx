import { cn } from "@pane/ui/cn";
import { useCallback, useEffect, useRef, useState } from "react";

import { HotkeyEvent } from "../../constants/hotkey-event";
import { PROFILE_COLOR_HEX } from "../../constants/profile-colors";
import { navigationStore, Page } from "../../stores/navigation-store";
import { profileStore } from "../../stores/profile-store";
import { tabStore } from "../../stores/tab-store";
import { trpc } from "../trpc";
import { groupMruTabs, initialSelectedIndex } from "./tab-switcher-grouping";

const MAX_VISIBLE_TABS = 8;

export function TabSwitcher({ onClose }: { onClose: () => void }) {
	const [groups] = useState(() =>
		groupMruTabs(
			tabStore.getState().mruHistory,
			profileStore.getState().profiles,
			MAX_VISIBLE_TABS,
		),
	);

	const flattened = groups.flatMap((group) => group.tabs);

	const [selectedIndex, setSelectedIndex] = useState(() =>
		initialSelectedIndex(flattened, tabStore.getState().mruHistory[1]),
	);

	const flattenedRef = useRef(flattened);
	flattenedRef.current = flattened;

	const selectedIndexRef = useRef(selectedIndex);
	selectedIndexRef.current = selectedIndex;

	const confirm = useCallback(
		(index: number) => {
			const tab = flattenedRef.current[index];

			if (tab) {
				navigationStore.getState().navigate(Page.BROWSER);
				trpc.tabs.switch.mutate({ tabId: tab.id });
			}

			onClose();
		},
		[onClose],
	);

	useEffect(() => {
		const subscription = trpc.hotkeys.events.subscribe(undefined, {
			onData(event: string) {
				if (event === HotkeyEvent.TAB_SWITCHER_FORWARD) {
					setSelectedIndex((prev) => (prev + 1) % flattenedRef.current.length);
				} else if (event === HotkeyEvent.TAB_SWITCHER_BACKWARD) {
					setSelectedIndex(
						(prev) =>
							(prev - 1 + flattenedRef.current.length) %
							flattenedRef.current.length,
					);
				}
			},
		});

		return () => subscription.unsubscribe();
	}, []);

	useEffect(() => {
		const handleKeyUp = (event: KeyboardEvent) => {
			if (event.key === "Control") {
				confirm(selectedIndexRef.current);
			}
		};

		const handleKeyDown = (event: KeyboardEvent) => {
			if (event.key === "Escape") {
				onClose();
			}
		};

		window.addEventListener("keyup", handleKeyUp);
		window.addEventListener("keydown", handleKeyDown);

		return () => {
			window.removeEventListener("keyup", handleKeyUp);
			window.removeEventListener("keydown", handleKeyDown);
		};
	}, [confirm, onClose]);

	if (flattened.length === 0) {
		onClose();

		return null;
	}

	const groupOffsets: number[] = [];

	let running = 0;

	for (const group of groups) {
		groupOffsets.push(running);
		running += group.tabs.length;
	}

	return (
		<div className="fixed inset-0 flex items-center justify-center">
			{/* biome-ignore lint/a11y: backdrop dismiss, Escape handled via keydown listener */}
			<div className="absolute inset-0 bg-black/40" onClick={onClose} />

			<div className="relative w-[320px] space-y-2 rounded-xl border border-white/10 bg-[#1a1a1e] p-1.5 shadow-2xl">
				{groups.map((group, groupIndex) => (
					<div key={group.id}>
						<div className="flex items-center gap-2 px-3 pt-1.5 pb-1">
							<div
								className="h-2 w-2 shrink-0 rounded-full"
								style={{
									backgroundColor: PROFILE_COLOR_HEX[group.color],
								}}
							/>

							<span className="truncate font-medium text-[11px] text-white/40">
								{group.name}
							</span>
						</div>

						{group.tabs.map((tab, tabIndex) => {
							const index = groupOffsets[groupIndex] + tabIndex;

							return (
								<button
									type="button"
									key={tab.id}
									className={cn(
										"flex w-full items-center gap-2.5 rounded-lg py-2 pr-3 pl-7 transition-colors",
										index === selectedIndex && "bg-white/10",
									)}
									onMouseDown={() => confirm(index)}
								>
									{tab.favicon ? (
										<img
											src={tab.favicon}
											alt=""
											className="h-4 w-4 shrink-0 rounded-sm"
										/>
									) : (
										<div className="h-4 w-4 shrink-0 rounded-sm bg-white/10" />
									)}

									<span className="truncate text-sm text-white/80">
										{tab.title}
									</span>
								</button>
							);
						})}
					</div>
				))}
			</div>
		</div>
	);
}
