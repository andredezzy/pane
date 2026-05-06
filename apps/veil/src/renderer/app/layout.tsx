import { cn } from "@pane/ui/cn";
import { Settings, Trash2, X } from "lucide-react";
import {
	Component,
	type ErrorInfo,
	type ReactNode,
	useCallback,
	useEffect,
	useRef,
	useState,
} from "react";
import { useStore } from "zustand/react";
import { useShallow } from "zustand/react/shallow";
import { createStore } from "zustand/vanilla";

import { navigationStore, Page } from "../../stores/navigation-store";
import { profileStore } from "../../stores/profile-store";
import { PinScreenMode, securityStore } from "../../stores/security-store";
import { tabStore } from "../../stores/tab-store";

import { ContentPanel } from "../components/content-panel";
import {
	ProfileBadge,
	ProfileHeader,
	ProfileItem,
	ProfileName,
	ProfileTabs,
} from "../components/sidebar/profile-item";
import {
	Sidebar,
	SidebarContent,
	SidebarFooter,
	SidebarHeader,
	SidebarNewButton,
	SidebarSeparator,
	SidebarSettingsButton,
	SidebarTitle,
} from "../components/sidebar/sidebar";
import {
	TabFavicon,
	TabItem,
	TabNew,
	TabTitle,
} from "../components/sidebar/tab-item";
import { CreateProfileSheet } from "../sheets/create-profile";
import { surface } from "../surface";
import { trpc } from "../trpc";
import { BrowserPage } from "./browser/page";
import { PinScreen } from "./lock-screen/page";
import { SettingsPage } from "./settings/page";

const expandedStore = createStore<{
	expanded: Set<string>;
	toggle: (id: string) => void;
}>()((set) => ({
	expanded: new Set<string>(),
	toggle: (id) =>
		set((s) => {
			const next = new Set(s.expanded);

			if (next.has(id)) {
				next.delete(id);
			} else {
				next.add(id);
			}

			return { expanded: next };
		}),
}));

export class ErrorBoundary extends Component<
	{ children: ReactNode },
	{ error: Error | null }
> {
	state: { error: Error | null } = { error: null };

	static getDerivedStateFromError(error: Error) {
		return { error };
	}

	componentDidCatch(error: Error, info: ErrorInfo) {
		console.error("[ErrorBoundary]", error, info.componentStack);
	}

	render() {
		if (this.state.error) {
			return (
				<div className="flex h-screen items-center justify-center bg-background text-foreground">
					<div className="space-y-2 text-center">
						<p className="font-medium text-sm">Something went wrong</p>
						<button
							type="button"
							className="text-muted-foreground text-xs hover:text-foreground"
							onClick={() => this.setState({ error: null })}
						>
							Try again
						</button>
					</div>
				</div>
			);
		}

		return this.props.children;
	}
}

function SidebarProfileItem({ id }: { id: string }) {
	const profile = useStore(profileStore, (s) =>
		s.profiles.find((p) => p.id === id),
	);

	const isExpanded = useStore(expandedStore, (s) => s.expanded.has(id));
	const activeTabId = useStore(tabStore, (s) => s.activeTabId);
	const page = useStore(navigationStore, (s) => s.page);

	const handleToggle = useCallback(() => {
		expandedStore.getState().toggle(id);

		if (!isExpanded) {
			trpc.profiles.load.mutate({ profileId: id });
		}
	}, [id, isExpanded]);

	if (!profile) {
		return null;
	}

	const isRunning = profile.tabs.length > 0;

	return (
		<ProfileItem>
			<ProfileHeader
				color={profile.color}
				active={isRunning}
				onClick={handleToggle}
			>
        <ProfileName>{profile.name}</ProfileName>

				{!isExpanded && isRunning ? (
					<ProfileBadge>{profile.tabs.length}</ProfileBadge>
        ) : null}

				<div className="w-0 shrink-0 transition-[width] duration-150 group-hover:w-3">
					<Trash2
						className="h-3 w-3 translate-x-2 text-muted-foreground opacity-0 transition-[transform,opacity] duration-150 group-hover:translate-x-0 group-hover:opacity-100"
						onClick={(e) => {
							e.stopPropagation();
							trpc.profiles.remove.mutate({ profileId: profile.id });
						}}
					/>
				</div>
			</ProfileHeader>

			{isExpanded ? (
				<ProfileTabs>
					{profile.tabs.map((tab) => (
						<TabItem
							key={tab.id}
							active={activeTabId === tab.id && page === Page.BROWSER}
							onClick={() => {
								navigationStore.getState().navigate(Page.BROWSER);
								trpc.tabs.switch.mutate({ tabId: tab.id });
							}}
						>
							<TabFavicon src={tab.favicon || undefined} />
							<TabTitle>{tab.title || "Loading..."}</TabTitle>
							<X
								className="h-3 w-3 shrink-0 opacity-0 transition-opacity group-hover:opacity-100"
								onClick={(e) => {
									e.stopPropagation();
									trpc.tabs.close.mutate({ tabId: tab.id });
								}}
							/>
						</TabItem>
					))}
					<TabNew
						onClick={() => {
							navigationStore.getState().navigate(Page.BROWSER);
							trpc.tabs.open.mutate({ profileId: profile.id });
						}}
					/>
				</ProfileTabs>
			) : null}
		</ProfileItem>
	);
}

export function Layout({ onReady }: { onReady?: () => void }) {
	const profileIds = useStore(
		profileStore,
		useShallow((s) => s.profiles.map((p) => p.id)),
	);

	const page = useStore(navigationStore, (s) => s.page);
	const { isLocked, pinScreenMode } = useStore(
		securityStore,
		useShallow((s) => ({ isLocked: s.isLocked, pinScreenMode: s.pinScreenMode })),
	);

	const showPinScreen = isLocked || pinScreenMode !== null;
	const [pinScreenActive, setPinScreenActive] = useState(showPinScreen);
	const [animatingOut, setAnimatingOut] = useState(false);
	const [animatingIn, setAnimatingIn] = useState(showPinScreen);
	const lastModeRef = useRef<"UNLOCK" | PinScreenMode | null>(null);

	if (showPinScreen) {
		lastModeRef.current = isLocked ? "UNLOCK" : pinScreenMode!;
	}

	useEffect(() => {
		if (showPinScreen && !pinScreenActive) {
			setPinScreenActive(true);
			setAnimatingIn(true);
			setAnimatingOut(false);
		} else if (!showPinScreen && pinScreenActive && !animatingOut) {
			setAnimatingOut(true);
		}
	}, [showPinScreen, pinScreenActive, animatingOut]);

	useEffect(() => {
		onReady?.();
	}, [onReady]);

	return (
		<div
			className="relative flex h-screen bg-background text-foreground"
			style={{ WebkitAppRegion: "drag" } as React.CSSProperties}
		>
			<Sidebar>
				<SidebarHeader>
					<SidebarTitle>Pane</SidebarTitle>
				</SidebarHeader>

				<SidebarContent>
					{profileIds.map((id) => (
						<SidebarProfileItem key={id} id={id} />
					))}
				</SidebarContent>

				<SidebarFooter>
					<SidebarNewButton onClick={() => surface.open(CreateProfileSheet)} />
					<SidebarSeparator />
					<SidebarSettingsButton
						active={page === Page.SETTINGS}
						onClick={() => navigationStore.getState().navigate(Page.SETTINGS)}
					>
						<Settings className="h-3.5 w-3.5" />
						Settings
					</SidebarSettingsButton>
				</SidebarFooter>
			</Sidebar>

			<ContentPanel>
				{page === Page.BROWSER ? <BrowserPage /> : null}
				{page === Page.SETTINGS ? <SettingsPage /> : null}
			</ContentPanel>

			{pinScreenActive && (
				<div className="absolute inset-0 z-50">
					<ContentPanel
						className={cn(
							"relative m-2 flex h-[calc(100%-16px)] items-center justify-center overflow-hidden",
							animatingIn && "animate-slide-up",
							animatingOut && "animate-slide-down",
						)}
						onAnimationEnd={() => {
							if (animatingIn) {
								setAnimatingIn(false);
							}
							if (animatingOut) {
								setPinScreenActive(false);
								setAnimatingOut(false);
							}
						}}
					>
						<PinScreen mode={lastModeRef.current!} />
					</ContentPanel>
				</div>
			)}
		</div>
	);
}
