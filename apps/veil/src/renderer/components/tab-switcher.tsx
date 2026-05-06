import { cn } from "@pane/ui/cn";
import { useCallback, useEffect, useRef, useState } from "react";
import { useStore } from "zustand/react";

import {
	PROFILE_COLOR_HEX,
	type ProfileColor,
} from "../../constants/profile-colors";
import { profileStore } from "../../stores/profile-store";
import { tabStore } from "../../stores/tab-store";

const MAX_VISIBLE_TABS = 8;

interface MruTab {
	id: string;
	title: string;
	favicon: string;
	profileId: string;
	profileName: string;
	profileColor: ProfileColor;
}

function resolveMruTabs(mruHistory: string[]): MruTab[] {
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
					profileId: profile.id,
					profileName: profile.name,
					profileColor: profile.color,
				});

				break;
			}
		}
	}

	return tabs;
}

export function TabSwitcher({
	visible,
	stepCounter,
	onConfirm,
	onCancel,
}: {
	visible: boolean;
	stepCounter: number;
	onConfirm: (tabId: string) => void;
	onCancel: () => void;
}) {
	const mruHistory = useStore(tabStore, (state) => state.mruHistory);
	const [selectedIndex, setSelectedIndex] = useState(1);
	const [tabs, setTabs] = useState<MruTab[]>([]);
	const lastStepRef = useRef(stepCounter);

	useEffect(() => {
		if (visible) {
			const resolved = resolveMruTabs(mruHistory);
			setTabs(resolved);
			setSelectedIndex(Math.min(1, resolved.length - 1));
			lastStepRef.current = stepCounter;
		}
	}, [visible, mruHistory, stepCounter]);

	useEffect(() => {
		if (!visible || tabs.length === 0 || stepCounter === lastStepRef.current) {
			return;
		}

		const delta = stepCounter - lastStepRef.current;
		lastStepRef.current = stepCounter;

		if (delta > 0) {
			setSelectedIndex((prev) => (prev + 1) % tabs.length);
		} else {
			setSelectedIndex((prev) => (prev - 1 + tabs.length) % tabs.length);
		}
	}, [stepCounter, visible, tabs.length]);

	const handleKeyUp = useCallback(
		(e: KeyboardEvent) => {
			if (e.key === "Control") {
				const tab = tabs[selectedIndex];

				if (tab) {
					onConfirm(tab.id);
				} else {
					onCancel();
				}
			}
		},
		[tabs, selectedIndex, onConfirm, onCancel],
	);

	const handleKeyDown = useCallback(
		(e: KeyboardEvent) => {
			if (e.key === "Escape") {
				onCancel();
			}
		},
		[onCancel],
	);

	useEffect(() => {
		if (!visible) {
			return;
		}

		window.addEventListener("keyup", handleKeyUp);
		window.addEventListener("keydown", handleKeyDown);

		return () => {
			window.removeEventListener("keyup", handleKeyUp);
			window.removeEventListener("keydown", handleKeyDown);
		};
	}, [visible, handleKeyUp, handleKeyDown]);

	if (!visible || tabs.length === 0) {
		return null;
	}

	return (
		<div className="absolute inset-0 z-[60] flex items-center justify-center">
			<div className="absolute inset-0 bg-black/40" />

			<div className="relative w-[320px] rounded-xl border border-white/10 bg-[#1a1a1e] p-1.5 shadow-2xl">
				{tabs.map((tab, index) => (
					<div
						key={tab.id}
						className={cn(
							"flex items-center gap-2.5 rounded-lg px-3 py-2 transition-colors",
							index === selectedIndex && "bg-white/10",
						)}
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
					</div>
				))}
			</div>
		</div>
	);
}
