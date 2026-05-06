import { PointerActivationConstraints, PointerSensor } from "@dnd-kit/dom";
import { DragDropProvider, DragOverlay } from "@dnd-kit/react";
import { isSortable, useSortable } from "@dnd-kit/react/sortable";
import { cn } from "@pane/ui/cn";
import { Settings, X } from "lucide-react";
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
import { navigationStore, Page } from "../../stores/navigation-store";
import { profileStore, type Tab } from "../../stores/profile-store";
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
import { icons, menuIcon } from "../menu/icons";
import { RestrictToVerticalAxis } from "../modifiers/restrict-to-vertical-axis";
import { ProfileSheet } from "../sheets/profile-sheet";
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
		preventActivation: () => false,
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
		<ProfileItem
			ref={ref}
			style={{ opacity: isDragSource ? 0.4 : 1 }}
			onContextMenu={async (event) => {
				event.preventDefault();

				const [editIcon, deleteIcon] = await Promise.all([
					menuIcon(icons.pencil),
					menuIcon(icons.trash),
				]);

				const selected = await trpc.ui.menu.mutate({
					items: [
						{ id: "edit", label: "Edit profile", icon: editIcon },
						{ type: "separator" },
						{ id: "delete", label: "Delete profile", icon: deleteIcon },
					],
				});

				if (selected === "edit") {
					surface.open(ProfileSheet, { profileId: profile.id });
				} else if (selected === "delete") {
					trpc.profiles.remove.mutate({ profileId: profile.id });
				}
			}}
		>
			<ProfileHeader
				ref={handleRef}
				color={profile.color}
				active={isRunning}
				onClick={handleToggle}
			>
				<ProfileName>{profile.name}</ProfileName>

				{!expanded && isRunning ? (
					<ProfileBadge>{profile.tabs.length}</ProfileBadge>
				) : null}
			</ProfileHeader>

			{expanded ? (
				<DragDropProvider
					sensors={SENSORS}
					modifiers={[RestrictToVerticalAxis]}
					onDragEnd={(event) => {
						if (event.canceled) {
							return;
						}

						const { source } = event.operation;

						if (isSortable(source)) {
							if (source.initialIndex !== source.index) {
								profileStore
									.getState()
									.reorderTabs(profile.id, source.initialIndex, source.index);
							}
						}
					}}
				>
					<ProfileTabs>
						{profile.tabs.map((tab, index) => (
							<SortableTab key={tab.id} tab={tab} index={index} />
						))}
						<TabNew
							onClick={() => {
								navigationStore.getState().navigate(Page.BROWSER);
								trpc.tabs.open.mutate({ profileId: profile.id });
							}}
						/>
					</ProfileTabs>

					<DragOverlay>
						{(source) => (
							<TabDragOverlay
								tabId={String(source.id)}
								profileId={profile.id}
							/>
						)}
					</DragOverlay>
				</DragDropProvider>
			) : null}
		</ProfileItem>
	);
}

function SortableTab({ tab, index }: { tab: Tab; index: number }) {
	const activeTabId = useStore(tabStore, (state) => state.activeTabId);
	const page = useStore(navigationStore, (state) => state.page);
	const { ref, isDragSource } = useSortable({ id: tab.id, index });

	return (
		<TabItem
			ref={ref}
			active={activeTabId === tab.id && page === Page.BROWSER}
			style={{ opacity: isDragSource ? 0.4 : 1 }}
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
	);
}

function TabDragOverlay({
	tabId,
	profileId,
}: {
	tabId: string;
	profileId: string;
}) {
	const tab = useStore(profileStore, (state) => {
		const profile = state.profiles.find((p) => p.id === profileId);

		return profile?.tabs.find((t) => t.id === tabId);
	});

	if (!tab) {
		return null;
	}

	return (
		<div className="w-[200px] scale-[1.02] cursor-grabbing">
			<div className="flex w-full items-center gap-1.5 rounded-[5px] bg-[rgba(255,255,255,0.05)] px-2 py-1 text-[#e4e4e7] text-[11px] shadow-lg">
				<TabFavicon src={tab.favicon || undefined} />
				<TabTitle>{tab.title || "Loading..."}</TabTitle>
			</div>
		</div>
	);
}

function ProfileDragOverlay({ profileId }: { profileId: string }) {
	const profile = useStore(profileStore, (state) =>
		state.profiles.find((p) => p.id === profileId),
	);

	if (!profile) {
		return null;
	}

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
							if (event.canceled) {
								return;
							}

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
							{(source) => <ProfileDragOverlay profileId={String(source.id)} />}
						</DragOverlay>
					</DragDropProvider>
				</SidebarContent>

				<SidebarFooter>
					<SidebarNewButton onClick={() => surface.open(ProfileSheet)} />
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
