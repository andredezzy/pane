import { PointerActivationConstraints, PointerSensor } from "@dnd-kit/dom";
import { DragDropProvider, DragOverlay } from "@dnd-kit/react";
import { isSortable, useSortable } from "@dnd-kit/react/sortable";
import { cn } from "@pane/ui/cn";
import { LogOut, Pencil, Settings, Trash2, X } from "lucide-react";
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
import { ProxyStatus, proxyStatusStore } from "../../stores/proxy-status-store";
import { PinScreenMode, securityStore } from "../../stores/security-store";
import { sidebarStore } from "../../stores/sidebar-store";
import { tabStore } from "../../stores/tab-store";
import { ContentPanel } from "../components/content-panel";
import {
	ProfileBadge,
	ProfileHeader,
	ProfileItem,
	ProfileName,
	ProfileProxyDot,
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
import { menuIcon } from "../menu/icons";
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

interface SidebarProfileItemProps {
	id: string;
	index: number;
	expanded: boolean;
	onToggle: (id: string) => void;
	onNewTab: (profileId: string) => void;
}

function SidebarProfileItem({
	id,
	index,
	expanded,
	onToggle,
	onNewTab,
}: SidebarProfileItemProps) {
	const profile = useStore(profileStore, (state) =>
		state.profiles.find((profile) => profile.id === id),
	);

	const isActiveProfile = useStore(
		tabStore,
		(state) => state.activeProfileId === id,
	);

	const proxyStatus = useStore(proxyStatusStore, (state) =>
		profile?.proxy ? state.statuses[id] : undefined,
	);

	const { ref, handleRef, isDragSource } = useSortable({ id, index });

	const proxyHost = profile?.proxy?.host;
	const proxyPort = profile?.proxy?.port;

	useEffect(() => {
		if (!profile?.proxy) {
			return;
		}

		const { set } = proxyStatusStore.getState();

		set(id, ProxyStatus.TESTING);

		trpc.profiles.testProxy
			.mutate({
				proxyType: profile.proxy.proxyType,
				host: profile.proxy.host,
				port: profile.proxy.port,
				username: profile.proxy.username ?? undefined,
				password: profile.proxy.password ?? undefined,
			})
			.then((result) => {
				set(id, result.success ? ProxyStatus.CONNECTED : ProxyStatus.FAILED);
			})
			.catch(() => {
				set(id, ProxyStatus.FAILED);
			});
	}, [id, proxyHost, proxyPort]);

	const handleToggle = useCallback(() => {
		onToggle(id);

		if (expanded) {
			trpc.profiles.unload.mutate({ profileId: id });
		} else {
			trpc.profiles.load.mutate({ profileId: id });
		}
	}, [id, expanded, onToggle]);

	if (!profile) {
		return null;
	}

	return (
		<ProfileItem
			ref={ref}
			style={{ opacity: isDragSource ? 0.4 : 1 }}
			onContextMenu={async (event) => {
				event.preventDefault();

				const [editIcon, signOutIcon, deleteIcon, googleSignedIn] =
					await Promise.all([
						menuIcon(Pencil),
						menuIcon(LogOut),
						menuIcon(Trash2),
						// Degrade to hiding the sign-out item on IPC error, so a failed
						// query can't reject Promise.all and swallow the whole menu.
						trpc.profiles.google.signedIn
							.query({ profileId: profile.id })
							.catch(() => false),
					]);

				const selected = await trpc.ui.menu.mutate({
					items: [
						{ id: "edit", label: "Edit profile", icon: editIcon },
						// Only offer sign-out when the profile actually has a Google session.
						...(googleSignedIn
							? [
									{
										id: "signout",
										label: "Sign out of Google",
										icon: signOutIcon,
									},
								]
							: []),
						{ type: "separator" },
						{ id: "delete", label: "Delete profile", icon: deleteIcon },
					],
				});

				if (selected === "edit") {
					surface.open(ProfileSheet, { profileId: profile.id });
				} else if (selected === "signout") {
					const confirmed = await trpc.ui.confirm.mutate({
						message: `Sign out of Google in "${profile.name}"?`,
						detail:
							"This clears Google cookies and site data (Gmail, YouTube, Drive) for this profile. You'll need to sign in again.",
						confirmLabel: "Sign out",
					});

					if (confirmed) {
						await trpc.profiles.google.signOut
							.mutate({ profileId: profile.id })
							.catch((error) => {
								console.error("[SignOut] Failed:", error);
							});
					}
				} else if (selected === "delete") {
					const confirmed = await trpc.ui.confirm.mutate({
						message: `Delete "${profile.name}"?`,
						detail: "This action cannot be undone.",
						confirmLabel: "Delete",
					});

					if (confirmed) {
						trpc.profiles.remove.mutate({ profileId: profile.id });
					}
				}
			}}
		>
			<ProfileHeader
				ref={handleRef}
				color={profile.color}
				active={isActiveProfile}
				onClick={handleToggle}
			>
				<ProfileName>{profile.name}</ProfileName>

				{proxyStatus !== undefined ? (
					<ProfileProxyDot status={proxyStatus} />
				) : null}

				{!expanded && profile.tabs.length > 0 ? (
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
						<TabNew onClick={() => onNewTab(profile.id)} />
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

interface SortableTabProps {
	tab: Tab;
	index: number;
}

function SortableTab({ tab, index }: SortableTabProps) {
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

interface TabDragOverlayProps {
	tabId: string;
	profileId: string;
}

function TabDragOverlay({ tabId, profileId }: TabDragOverlayProps) {
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

	const isActiveProfile = useStore(
		tabStore,
		(state) => state.activeProfileId === profileId,
	);

	if (!profile) {
		return null;
	}

	return (
		<div className="w-[200px] scale-[1.02] cursor-grabbing">
			<ProfileHeader
				className="shadow-lg"
				color={profile.color}
				active={isActiveProfile}
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

	const expandedProfileIds = useStore(
		sidebarStore,
		useShallow((state) => state.expandedProfileIds),
	);

	const toggleExpanded = useStore(sidebarStore, (state) => state.toggleProfile);

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
				if (event !== HotkeyEvent.FOCUS_ADDRESS_BAR || page !== Page.BROWSER) {
					return;
				}

				const tryFocus = (remaining: number) => {
					const input = addressBarRef.current;

					if (input) {
						input.focus();
						input.select();
					} else if (remaining > 0) {
						requestAnimationFrame(() => tryFocus(remaining - 1));
					}
				};

				tryFocus(30);
			},
			[page],
		),
	);

	const handleNewTab = useCallback(async (profileId: string) => {
		navigationStore.getState().navigate(Page.BROWSER);
		await trpc.tabs.open.mutate({ profileId, focusAddressBar: true });

		const tryFocus = (remaining: number) => {
			const input = addressBarRef.current;

			if (input) {
				input.focus();
				input.select();
			} else if (remaining > 0) {
				requestAnimationFrame(() => tryFocus(remaining - 1));
			}
		};

		tryFocus(30);
	}, []);

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
								expanded={expandedProfileIds.includes(id)}
								onToggle={toggleExpanded}
								onNewTab={handleNewTab}
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
