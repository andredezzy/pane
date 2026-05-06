import { useState } from "react";
import { useStore } from "zustand/react";
import { useShallow } from "zustand/react/shallow";

import {
	ProfileColor,
	type ProfileColor as ProfileColorType,
} from "../../../constants/profile-colors";
import { extensionStore } from "../../../stores/extension-store";
import { profileStore } from "../../../stores/profile-store";
import { tabStore } from "../../../stores/tab-store";
import { trpc } from "../../trpc";
import { BrowserActionList } from "./_components/browser-action-list";
import { EmptyState } from "./_components/empty-state";
import {
	Toolbar,
	ToolbarAddress,
	ToolbarExtensions,
	ToolbarNavigation,
	ToolbarNavigationBack,
	ToolbarNavigationForward,
	ToolbarNavigationReload,
	ToolbarProfile,
} from "./_components/toolbar";

function BrowserToolbar() {
	const { activeTabId, activeProfileId } = useStore(
		tabStore,
		useShallow((state) => ({
			activeTabId: state.activeTabId,
			activeProfileId: state.activeProfileId,
		})),
	);

	const loadingTabIds = useStore(tabStore, (state) => state.loadingTabIds);

	const profiles = useStore(profileStore, (state) => state.profiles);
	const extensions = useStore(extensionStore, (state) => state.extensions);

	const activeProfile = profiles.find(
		(profile) => profile.id === activeProfileId,
	);
	const activeTab = activeProfile?.tabs.find((tab) => tab.id === activeTabId);

	const activeUrl = activeTab?.url ?? "";
	const profileName = activeProfile?.name ?? "";

	const profileColor: ProfileColorType =
		activeProfile?.color ?? ProfileColor.BLUE;

	const profileExtensions = activeProfileId
		? extensions[activeProfileId]
		: undefined;

	const [inputValue, setInputValue] = useState("");
	const [isFocused, setIsFocused] = useState(false);

	const displayUrl = isFocused ? inputValue : activeUrl;

	const isLoading = activeTabId ? loadingTabIds.includes(activeTabId) : false;

	const handleSubmit = (e: React.FormEvent) => {
		e.preventDefault();

		if (inputValue.trim()) {
			trpc.tabs.navigate.mutate({ url: inputValue.trim() });
			(document.activeElement as HTMLElement)?.blur();
		}
	};

	if (!activeTabId) {
		return null;
	}

	return (
		<Toolbar>
			<ToolbarNavigation>
				<ToolbarNavigationBack onClick={() => trpc.tabs.goBack.mutate()} />
				<ToolbarNavigationForward
					onClick={() => trpc.tabs.goForward.mutate()}
				/>
				<ToolbarNavigationReload
					isLoading={isLoading}
					onClick={() =>
						isLoading ? trpc.tabs.stop.mutate() : trpc.tabs.reload.mutate()
					}
				/>
			</ToolbarNavigation>

			<form onSubmit={handleSubmit} className="flex flex-1">
				<ToolbarAddress
					isLoading={isLoading}
					value={displayUrl}
					onChange={(e) => setInputValue(e.target.value)}
					onFocus={() => {
						setInputValue(activeUrl);
						setIsFocused(true);
					}}
					onBlur={() => setIsFocused(false)}
					placeholder="Search or enter URL"
				/>
			</form>

			<ToolbarExtensions>
				{profileExtensions && profileExtensions.length > 0 && (
					<BrowserActionList partition={`persist:profile-${activeProfileId}`} />
				)}
			</ToolbarExtensions>

			{profileName ? (
				<ToolbarProfile color={profileColor}>{profileName}</ToolbarProfile>
			) : null}
		</Toolbar>
	);
}

export function BrowserPage() {
	const activeTabId = useStore(tabStore, (state) => state.activeTabId);

	return (
		<>
			{activeTabId ? <BrowserToolbar /> : null}
			{!activeTabId ? <EmptyState /> : null}
		</>
	);
}
