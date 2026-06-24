import { cn } from "@pane/ui/cn";
import { useCallback, useEffect, useRef, useState } from "react";

import { HotkeyEvent } from "../../constants/hotkey-event";
import { PROFILE_COLOR_HEX } from "../../constants/profile-colors";
import { navigationStore, Page } from "../../stores/navigation-store";
import { profileStore } from "../../stores/profile-store";
import { tabStore } from "../../stores/tab-store";
import { trpc } from "../trpc";
import {
	computeGroupOffsets,
	cycleIndex,
	groupMruTabs,
	initialSelectedIndex,
} from "./tab-switcher-grouping";

const MAX_VISIBLE_TABS = 8;

export function TabSwitcher({ onClose }: { onClose: () => void }) {
	const [{ groups, flattened }] = useState(() => {
		const groups = groupMruTabs(
			tabStore.getState().mruHistory,
			profileStore.getState().profiles,
			MAX_VISIBLE_TABS,
		);

		return { groups, flattened: groups.flatMap((group) => group.tabs) };
	});

	const [selectedIndex, setSelectedIndex] = useState(() =>
		initialSelectedIndex(flattened, tabStore.getState().mruHistory[1]),
	);

	// selectedIndex changes are read by the Control-release handler, whose effect
	// is not re-run on every selection; the ref keeps it from going stale.
	const selectedIndexRef = useRef(selectedIndex);
	selectedIndexRef.current = selectedIndex;

	const buttonRefs = useRef<(HTMLButtonElement | null)[]>([]);

	const confirm = useCallback(
		(index: number) => {
			const tab = flattened[index];

			if (tab) {
				navigationStore.getState().navigate(Page.BROWSER);
				trpc.tabs.switch.mutate({ tabId: tab.id });
			}

			onClose();
		},
		[flattened, onClose],
	);

	useEffect(() => {
		if (flattened.length === 0) {
			return;
		}

		const subscription = trpc.hotkeys.events.subscribe(undefined, {
			onData(event: string) {
				if (event === HotkeyEvent.TAB_SWITCHER_FORWARD) {
					setSelectedIndex((prev) => cycleIndex(prev, 1, flattened.length));
				} else if (event === HotkeyEvent.TAB_SWITCHER_BACKWARD) {
					setSelectedIndex((prev) => cycleIndex(prev, -1, flattened.length));
				}
			},
		});

		return () => subscription.unsubscribe();
	}, [flattened]);

	useEffect(() => {
		if (flattened.length === 0) {
			return;
		}

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
	}, [confirm, flattened, onClose]);

	useEffect(() => {
		if (flattened.length === 0) {
			onClose();
		}
	}, [flattened, onClose]);

	// Move DOM focus onto the highlighted row so assistive tech follows the
	// selection while the overlay (a modal dialog) is open. The selection ring is
	// state-driven (below), so it shows regardless of :focus-visible heuristics.
	useEffect(() => {
		buttonRefs.current[selectedIndex]?.focus({ preventScroll: true });
	}, [selectedIndex]);

	if (flattened.length === 0) {
		return null;
	}

	const groupOffsets = computeGroupOffsets(groups);

	return (
		<div className="fixed inset-0 flex items-center justify-center">
			{/* biome-ignore lint/a11y/noStaticElementInteractions: backdrop is a click-to-dismiss scrim; Escape is handled by the window keydown listener */}
			{/* biome-ignore lint/a11y/useKeyWithClickEvents: backdrop is a click-to-dismiss scrim; Escape is handled by the window keydown listener */}
			<div className="absolute inset-0 bg-black/40" onClick={onClose} />

			<div
				role="dialog"
				aria-modal="true"
				aria-label="Tab switcher"
				className="relative w-[320px] space-y-2 rounded-xl border border-white/10 bg-[#1a1a1e] p-1.5 shadow-2xl"
			>
				{groups.map((group, groupIndex) => {
					const headerId = `tab-switcher-group-${group.id}`;

					return (
						// biome-ignore lint/a11y/useSemanticElements: groups a profile's tabs for assistive tech; <fieldset> is form-specific and wrong here
						<div key={group.id} role="group" aria-labelledby={headerId}>
							<div className="flex items-center gap-2 px-3 pt-1.5 pb-1">
								<div
									className="h-2 w-2 shrink-0 rounded-full"
									style={{
										backgroundColor: PROFILE_COLOR_HEX[group.color],
									}}
								/>

								<span
									id={headerId}
									className="truncate font-medium text-[11px] text-white/40"
								>
									{group.name}
								</span>
							</div>

							{group.tabs.map((tab, tabIndex) => {
								const index = groupOffsets[groupIndex] + tabIndex;

								return (
									<button
										type="button"
										key={tab.id}
										ref={(element) => {
											buttonRefs.current[index] = element;
										}}
										aria-current={index === selectedIndex ? "true" : undefined}
										className={cn(
											"flex w-full items-center gap-2.5 rounded-lg py-2 pr-3 pl-7 outline-none transition",
											index === selectedIndex &&
												"bg-white/10 ring-1 ring-white/25",
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
					);
				})}
			</div>
		</div>
	);
}
