import { cn } from "@pane/ui/cn";
import { DragDropProvider, DragOverlay } from "@dnd-kit/react";
import { useSortable, isSortable } from "@dnd-kit/react/sortable";
import {
	PointerSensor,
	PointerActivationConstraints,
} from "@dnd-kit/dom";
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
import { RestrictToVerticalAxis } from "../modifiers/restrict-to-vertical-axis";

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
import { HotkeyEvent, useHotkeyEvents } from "../hooks/use-hotkey-events";
import { CreateProfileSheet } from "../sheets/create-profile";
import { surface } from "../surface";
import { trpc } from "../trpc";
import { BrowserPage } from "./browser/page";
import { PinScreen } from "./lock-screen/page";
import { SettingsPage } from "./settings/page";

const SENSORS = [
	PointerSensor.configure({
		activationConstraints: [
			new PointerActivationConstraints.Distance({ value: 5 }),
		],
	}),
];

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

function SidebarProfileItem({
	id,
	index,
	expanded,
	onToggle,
}: {
	id: string;
	index: number;
	expanded: boolean;
	onToggle: (id: string) => void;
}) {
	const profile = useStore(profileStore, (state) =>
		state.profiles.find((profile) => profile.id === id),
	);

	const activeTabId = useStore(tabStore, (state) => state.activeTabId);
	const page = useStore(navigationStore, (state) => state.page);

	const { ref, handleRef, isDragSource } = useSortable({ id, index });

	const handleToggle = useCallback(() => {
		onToggle(id);

		if (!expanded) {
			trpc.profiles.load.mutate({ profileId: id });
		}
	}, [id, expanded, onToggle]);

	if (!profile) {
		return null;
	}

	const isRunning = profile.tabs.length > 0;

	return (
		<ProfileItem ref={ref} style={{ opacity: isDragSource ? 0.4 : 1 }}>
			<ProfileHeader
				ref={handleRef}
				className="cursor-grab"
				color={profile.color}
				active={isRunning}
				onClick={handleToggle}
			>
				<ProfileName>{profile.name}</ProfileName>

				{!expanded && isRunning ? (
					<ProfileBadge>{profile.tabs.length}</ProfileBadge>
				) : null}

				<div className="w-0 shrink-0 transition-[width] duration-150 group-hover:w-3">
					<Trash2
						className="h-3 w-3 translate-x-2 text-muted-foreground opacity-0 transition-[transform,opacity] duration-150 group-hover:translate-x-0 group-hover:opacity-100"
						onClick={(event) => {
							event.stopPropagation();
							trpc.profiles.remove.mutate({ profileId: profile.id });
						}}
					/>
				</div>
			</ProfileHeader>

			{expanded ? (
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
								onClick={(event) => {
									event.stopPropagation();
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

function ProfileDragOverlay({ profileId }: { profileId: string }) {
	const profile = useStore(profileStore, (state) =>
		state.profiles.find((p) => p.id === profileId),
	);

	if (!profile) return null;

	return (
		<div className="w-[200px] scale-[1.02] cursor-grabbing">
			<ProfileHeader
				className="shadow-lg"
				color={profile.color}
				active={profile.tabs.length > 0}
			>
				<ProfileName>{profile.name}</ProfileName>
			</ProfileHeader>
		</div>
	);
}

enum PinScreenPhase {
	HIDDEN = "HIDDEN",
	ANIMATING_IN = "ANIMATING_IN",
	VISIBLE = "VISIBLE",
	ANIMATING_OUT = "ANIMATING_OUT",
}

export function Layout({ onReady }: { onReady?: () => void }) {
	const profileIds = useStore(
		profileStore,
		useShallow((state) => state.profiles.map((profile) => profile.id)),
	);

	const [expanded, setExpanded] = useState<Set<string>>(new Set());

	const toggleExpanded = useCallback((id: string) => {
		setExpanded((prev) => {
			const next = new Set(prev);

			if (next.has(id)) {
				next.delete(id);
			} else {
				next.add(id);
			}

			return next;
		});
	}, []);

	const page = useStore(navigationStore, (state) => state.page);

	const { isLocked, pinScreenMode } = useStore(
		securityStore,
		useShallow((state) => ({
			isLocked: state.isLocked,
			pinScreenMode: state.pinScreenMode,
		})),
	);

	const showPinScreen = isLocked || pinScreenMode !== null;

	const [pinPhase, setPinPhase] = useState(
		showPinScreen ? PinScreenPhase.ANIMATING_IN : PinScreenPhase.HIDDEN,
	);

	const lastModeRef = useRef(PinScreenMode.UNLOCK);

	if (showPinScreen) {
		lastModeRef.current = isLocked
			? PinScreenMode.UNLOCK
			: (pinScreenMode ?? PinScreenMode.UNLOCK);
	}

	useEffect(() => {
		if (showPinScreen && pinPhase === PinScreenPhase.HIDDEN) {
			setPinPhase(PinScreenPhase.ANIMATING_IN);
		} else if (!showPinScreen && pinPhase === PinScreenPhase.VISIBLE) {
			setPinPhase(PinScreenPhase.ANIMATING_OUT);
		}
	}, [showPinScreen, pinPhase]);

	const addressBarRef = useRef<HTMLInputElement>(null);

	useHotkeyEvents(
		useCallback(
			(event: HotkeyEvent) => {
				if (event === HotkeyEvent.FOCUS_ADDRESS_BAR && page === Page.BROWSER) {
					addressBarRef.current?.focus();
					addressBarRef.current?.select();
				}
			},
			[page],
		),
	);

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
					<DragDropProvider
						sensors={SENSORS}
						modifiers={[RestrictToVerticalAxis]}
						onDragEnd={(event) => {
							if (event.canceled) return;

							const { source } = event.operation;

							if (isSortable(source)) {
								if (source.initialIndex !== source.index) {
									profileStore
										.getState()
										.reorderProfiles(source.initialIndex, source.index);
								}
							}
						}}
					>
						{profileIds.map((id, index) => (
							<SidebarProfileItem
								key={id}
								id={id}
								index={index}
								expanded={expanded.has(id)}
								onToggle={toggleExpanded}
							/>
						))}

						<DragOverlay>
							{(source) => (
								<ProfileDragOverlay profileId={String(source.id)} />
							)}
						</DragOverlay>
					</DragDropProvider>
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
				{page === Page.BROWSER ? (
					<BrowserPage addressBarRef={addressBarRef} />
				) : null}
				{page === Page.SETTINGS ? <SettingsPage /> : null}
			</ContentPanel>

			{pinPhase !== PinScreenPhase.HIDDEN && (
				<div className="absolute inset-0 z-50">
					<ContentPanel
						style={{ WebkitAppRegion: "drag" } as React.CSSProperties}
						className={cn(
							"relative m-2 flex h-[calc(100%-16px)] items-center justify-center overflow-hidden",
							pinPhase === PinScreenPhase.ANIMATING_IN && "animate-slide-up",
							pinPhase === PinScreenPhase.ANIMATING_OUT && "animate-slide-down",
						)}
						onAnimationEnd={() => {
							if (pinPhase === PinScreenPhase.ANIMATING_IN) {
								setPinPhase(PinScreenPhase.VISIBLE);
							} else if (pinPhase === PinScreenPhase.ANIMATING_OUT) {
								setPinPhase(PinScreenPhase.HIDDEN);
							}
						}}
					>
						<PinScreen mode={lastModeRef.current} />
					</ContentPanel>
				</div>
			)}
		</div>
	);
}
