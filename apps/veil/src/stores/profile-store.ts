import { createJSONStorage, persist } from "zustand/middleware";
import { createStore } from "zustand/vanilla";
import { ProfileColor } from "../constants/profile-colors";
import { fsStorage } from "./middlewares/fs-storage";
import { sync } from "./middlewares/sync";

export interface Fingerprint {
	userAgent: string;
	platform: "windows" | "macos" | "linux";
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
	proxyType: "http" | "https" | "socks4" | "socks5";
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
	notes: string | null;
	fingerprint: Fingerprint;
	proxy: ProxyConfig | null;
	tags: string[];
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
	remove: (id: string) => void;
	openTab: (profileId: string, tabId: string, url: string) => void;
	closeTab: (profileId: string, tabId: string) => void;
	updateTab: (profileId: string, tabId: string, partial: Partial<Tab>) => void;
}

export const profileStore = createStore<ProfileState>()(
	persist(
		sync(
			(set) => ({
				profiles: [],

				create: (input) => {
					const id = crypto.randomUUID();
					const now = new Date().toISOString();

					set((s) => ({
						profiles: [
							...s.profiles,
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

				remove: (id) => {
					set((s) => ({
						profiles: s.profiles.filter((p) => p.id !== id),
					}));
				},

				openTab: (profileId, tabId, url) => {
					set((s) => ({
						profiles: s.profiles.map((p) =>
							p.id === profileId
								? {
										...p,
										tabs: [
											...p.tabs,
											{
												id: tabId,
												url,
												title: "New Tab",
												favicon: "",
												isLoaded: true,
											},
										],
									}
								: p,
						),
					}));
				},

				closeTab: (profileId, tabId) => {
					set((s) => ({
						profiles: s.profiles.map((p) =>
							p.id === profileId
								? { ...p, tabs: p.tabs.filter((t) => t.id !== tabId) }
								: p,
						),
					}));
				},

				updateTab: (profileId, tabId, partial) => {
					set((s) => ({
						profiles: s.profiles.map((p) =>
							p.id === profileId
								? {
										...p,
										tabs: p.tabs.map((t) =>
											t.id === tabId ? { ...t, ...partial } : t,
										),
									}
								: p,
						),
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
						(p) => {
							const fp = p.fingerprint as PersistedFingerprint;

							return {
								...p,
								color: p.color ?? ProfileColor.BLUE,
								tabs: p.tabs.map((t) => ({
									...t,
									favicon: t.favicon ?? "",
									isLoaded: false,
								})),
								fingerprint: {
									...fp,
									screen: {
										...fp.screen,
										colorDepth: fp.screen.colorDepth ?? 24,
									},
									canvas: fp.canvas ?? { noise: true },
									audio: fp.audio ?? { noise: true },
								},
							};
						},
					),
				};
			},
		},
	),
);
