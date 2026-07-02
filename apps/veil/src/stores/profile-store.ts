import { createJSONStorage, persist } from "zustand/middleware";
import { createStore } from "zustand/vanilla";
import { ProfileColor } from "../constants/profile-colors";
import { fsStorage } from "./middlewares/fs-storage";
import { sync } from "./middlewares/sync";

export enum Platform {
	WINDOWS = "WINDOWS",
	MACOS = "MACOS",
	LINUX = "LINUX",
}

export enum ProxyType {
	HTTP = "HTTP",
	HTTPS = "HTTPS",
	SOCKS4 = "SOCKS4",
	SOCKS5 = "SOCKS5",
}

export interface Fingerprint {
	userAgent: string;
	platform: Platform;
	screen: { width: number; height: number; colorDepth: number };
	language: string;
	languages: string[];
	timezone: string;
	webgl: { vendor: string; renderer: string } | null;
	hardwareConcurrency: number;
	deviceMemory: number;
	maxTouchPoints: number;
	canvas: { noise: boolean };
	audio: { noise: boolean };
}

export interface ProxyConfig {
	proxyType: ProxyType;
	host: string;
	port: number;
	username: string | null;
	password: string | null;
}

export interface Tab {
	id: string;
	url: string;
	title: string;
	favicon: string;
	isLoaded: boolean;
}

export interface BrowserProfile {
	id: string;
	name: string;
	color: ProfileColor;
	group: string | null;
	// null = spoofing disabled: the profile presents the real (Electron/pane-stripped)
	// Chrome surface untouched. Heavy JS spoofing is itself detectable, so a clean real
	// fingerprint is often the better choice against strict bot checks (Cloudflare).
	fingerprint: Fingerprint | null;
	proxy: ProxyConfig | null;
	tabs: Tab[];
	createdAt: string;
	updatedAt: string;
}

type CreateInput = Omit<
	BrowserProfile,
	"id" | "createdAt" | "updatedAt" | "tabs"
>;

interface ProfileState {
	profiles: BrowserProfile[];

	create: (input: CreateInput) => string;
	update: (id: string, input: Partial<CreateInput>) => void;
	remove: (id: string) => void;
	openTab: (profileId: string, tabId: string, url: string) => void;
	closeTab: (profileId: string, tabId: string) => void;
	updateTab: (profileId: string, tabId: string, partial: Partial<Tab>) => void;
	reorderProfiles: (fromIndex: number, toIndex: number) => void;
	reorderTabs: (profileId: string, fromIndex: number, toIndex: number) => void;
}

export const profileStore = createStore<ProfileState>()(
	persist(
		sync(
			(set) => ({
				profiles: [],

				create: (input) => {
					const id = crypto.randomUUID();
					const now = new Date().toISOString();

					set((state) => ({
						profiles: [
							...state.profiles,
							{
								...input,
								id,
								tabs: [],
								createdAt: now,
								updatedAt: now,
							},
						],
					}));

					return id;
				},

				update: (id, input) => {
					set((state) => ({
						profiles: state.profiles.map((profile) =>
							profile.id === id
								? {
										...profile,
										...input,
										updatedAt: new Date().toISOString(),
									}
								: profile,
						),
					}));
				},

				remove: (id) => {
					set((state) => ({
						profiles: state.profiles.filter((profile) => profile.id !== id),
					}));
				},

				openTab: (profileId, tabId, url) => {
					set((state) => ({
						profiles: state.profiles.map((profile) =>
							profile.id === profileId
								? {
										...profile,
										tabs: [
											...profile.tabs,
											{
												id: tabId,
												url,
												title: "New tab",
												favicon: "",
												isLoaded: true,
											},
										],
									}
								: profile,
						),
					}));
				},

				closeTab: (profileId, tabId) => {
					set((state) => ({
						profiles: state.profiles.map((profile) =>
							profile.id === profileId
								? {
										...profile,
										tabs: profile.tabs.filter((tab) => tab.id !== tabId),
									}
								: profile,
						),
					}));
				},

				updateTab: (profileId, tabId, partial) => {
					set((state) => ({
						profiles: state.profiles.map((profile) =>
							profile.id === profileId
								? {
										...profile,
										tabs: profile.tabs.map((tab) =>
											tab.id === tabId ? { ...tab, ...partial } : tab,
										),
									}
								: profile,
						),
					}));
				},

				reorderProfiles: (fromIndex, toIndex) => {
					set((state) => {
						const profiles = [...state.profiles];
						const [moved] = profiles.splice(fromIndex, 1);
						profiles.splice(toIndex, 0, moved);

						return { profiles };
					});
				},

				reorderTabs: (profileId, fromIndex, toIndex) => {
					set((state) => ({
						profiles: state.profiles.map((profile) => {
							if (profile.id !== profileId) {
								return profile;
							}

							const tabs = [...profile.tabs];
							const [moved] = tabs.splice(fromIndex, 1);
							tabs.splice(toIndex, 0, moved);

							return { ...profile, tabs };
						}),
					}));
				},
			}),
			{ name: "profile-store" },
		),
		{
			name: "profiles",
			storage: createJSONStorage(() => fsStorage),
			skipHydration: true,
			partialize: (state) => ({
				profiles: state.profiles.map((profile) => ({
					...profile,
					tabs: profile.tabs.map(({ isLoaded, ...tab }) => tab),
				})),
			}),
			merge: (persisted, current) => {
				type PersistedFingerprint = Omit<
					Fingerprint,
					"screen" | "canvas" | "audio"
				> & {
					screen: { width: number; height: number; colorDepth?: number };
					canvas?: { noise: boolean };
					audio?: { noise: boolean };
				};

				return {
					...current,
					...(persisted as Partial<ProfileState>),
					profiles: ((persisted as Partial<ProfileState>)?.profiles ?? []).map(
						(profile) => {
							const fingerprint =
								profile.fingerprint as PersistedFingerprint | null;

							return {
								...profile,
								color: profile.color ?? ProfileColor.BLUE,
								tabs: profile.tabs.map((tab) => ({
									...tab,
									favicon: tab.favicon ?? "",
									isLoaded: false,
								})),
								fingerprint: fingerprint
									? {
											...fingerprint,
											screen: {
												...fingerprint.screen,
												colorDepth: fingerprint.screen.colorDepth ?? 24,
											},
											canvas: fingerprint.canvas ?? { noise: true },
											audio: fingerprint.audio ?? { noise: true },
										}
									: null,
							};
						},
					),
				};
			},
		},
	),
);
