import { cn } from "@pane/ui/cn";
import { useCallback, useEffect, useRef, useState } from "react";

import { HotkeyEvent } from "../../constants/hotkey-event";
import {
	PROFILE_COLOR_HEX,
	type ProfileColor,
} from "../../constants/profile-colors";
import { navigationStore, Page } from "../../stores/navigation-store";
import { profileStore } from "../../stores/profile-store";
import { tabStore } from "../../stores/tab-store";
import { trpc } from "../trpc";

const MAX_VISIBLE_TABS = 8;

interface MruTab {
	id: string;
	title: string;
	favicon: string;
	profileColor: ProfileColor;
}

function resolveMruTabs(): MruTab[] {
	const { mruHistory } = tabStore.getState();
	const profiles = profileStore.getState().profiles;
	const tabs: MruTab[] = [];

	for (const tabId of mruHistory) {
		if (tabs.length >= MAX_VISIBLE_TABS) {
			break;
		}

		for (const profile of profiles) {
			const tab = profile.tabs.find((tab) => tab.id === tabId);

			if (tab) {
				tabs.push({
					id: tab.id,
					title: tab.title || "Loading...",
					favicon: tab.favicon || "",
					profileColor: profile.color,
				});

				break;
			}
		}
	}

	return tabs;
}

export function TabSwitcher({ onClose }: { onClose: () => void }) {
	const [tabs] = useState(resolveMruTabs);
	const [selectedIndex, setSelectedIndex] = useState(
		Math.min(1, tabs.length - 1),
	);

	const tabsRef = useRef(tabs);
	const selectedIndexRef = useRef(selectedIndex);
	selectedIndexRef.current = selectedIndex;

	const confirm = useCallback(
		(index: number) => {
			const tab = tabsRef.current[index];

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
					setSelectedIndex((prev) => (prev + 1) % tabsRef.current.length);
				} else if (event === HotkeyEvent.TAB_SWITCHER_BACKWARD) {
					setSelectedIndex(
						(prev) =>
							(prev - 1 + tabsRef.current.length) % tabsRef.current.length,
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

	if (tabs.length === 0) {
		onClose();

		return null;
	}

	return (
		<div className="fixed inset-0 flex items-center justify-center">
			{/* biome-ignore lint/a11y: backdrop dismiss, Escape handled via keydown listener */}
			<div className="absolute inset-0 bg-black/40" onClick={onClose} />

			<div className="relative w-[320px] rounded-xl border border-white/10 bg-[#1a1a1e] p-1.5 shadow-2xl">
				{tabs.map((tab, index) => (
					<button
						type="button"
						key={tab.id}
						className={cn(
							"flex w-full items-center gap-2.5 rounded-lg px-3 py-2 transition-colors hover:bg-white/5",
							index === selectedIndex && "bg-white/10",
						)}
						onClick={() => confirm(index)}
					>
						<div
							className="h-2 w-2 shrink-0 rounded-full"
							style={{
								backgroundColor: PROFILE_COLOR_HEX[tab.profileColor],
							}}
						/>

						{tab.favicon ? (
							<img
								src={tab.favicon}
								alt=""
								className="h-4 w-4 shrink-0 rounded-sm"
							/>
						) : (
							<div className="h-4 w-4 shrink-0 rounded-sm bg-white/10" />
						)}

						<span className="truncate text-sm text-white/80">{tab.title}</span>
					</button>
				))}
			</div>
		</div>
	);
}
