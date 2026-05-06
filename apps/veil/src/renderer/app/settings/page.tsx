import { zodResolver } from "@hookform/resolvers/zod";
import { Button } from "@pane/ui/components/button";
import {
	Form,
	FormControl,
	FormField,
	FormItem,
	FormLabel,
	FormMessage,
} from "@pane/ui/components/form";
import { Input } from "@pane/ui/components/input";
import { useCallback, useEffect, useState } from "react";
import { useForm } from "react-hook-form";
import { z } from "zod/v4";
import { useStore } from "zustand/react";
import { PinScreenMode, securityStore } from "../../../stores/security-store";
import { settingsStore } from "../../../stores/settings-store";
import { trpc } from "../../trpc";
import {
	ExtensionInstallForm,
	ExtensionItem,
	ExtensionList,
	type InstalledExtension,
	parseExtensionId,
	UninstallDialog,
} from "./_components/extension-settings";

const settingsSchema = z.object({
	chromiumPath: z.string().min(1, "Browser path is required"),
});

type SettingsValues = z.infer<typeof settingsSchema>;

export function SettingsPage() {
	const settings = useStore(settingsStore, (state) => state.settings);
	const pin = useStore(securityStore, (state) => state.pin);

	const form = useForm<SettingsValues>({
		resolver: zodResolver(settingsSchema),
		defaultValues: { chromiumPath: settings.chromiumPath },
	});

	useEffect(() => {
		form.reset({ chromiumPath: settings.chromiumPath });
	}, [settings.chromiumPath, form]);

	const onSubmit = (data: SettingsValues) => {
		settingsStore.getState().update({ chromiumPath: data.chromiumPath });
	};

	const [extensions, setExtensions] = useState<InstalledExtension[]>([]);
	const [isInstalling, setIsInstalling] = useState(false);

	const [uninstallTarget, setUninstallTarget] =
		useState<InstalledExtension | null>(null);

	useEffect(() => {
		trpc.cws.installed.query().then(setExtensions);
	}, []);

	const handleInstall = useCallback(async (value: string) => {
		const id = parseExtensionId(value);

		if (!id) {
			return;
		}

		setIsInstalling(true);

		try {
			const ext = await trpc.cws.install.mutate({ extensionId: id });

			if (ext) {
				const updated = await trpc.cws.installed.query();
				setExtensions(updated);
			}
		} catch {
		} finally {
			setIsInstalling(false);
		}
	}, []);

	const handleUninstall = useCallback(async () => {
		if (!uninstallTarget) {
			return;
		}

		try {
			await trpc.cws.uninstall.mutate({ extensionId: uninstallTarget.id });

			setExtensions((prev) => prev.filter((extension) => extension.id !== uninstallTarget.id));

			setUninstallTarget(null);
		} catch {}
	}, [uninstallTarget]);

	return (
		<div className="flex-1 overflow-auto px-10 py-8">
			<div className="max-w-[480px] space-y-6">
				<div>
					<h1 className="font-medium text-accent-foreground text-sm">
						Settings
					</h1>
					<p className="text-[#71717a] text-xs">
						Manage your application preferences.
					</p>
				</div>

				<div className="h-px bg-[rgba(255,255,255,0.05)]" />

				<div>
					<span className="font-medium text-[10px] text-muted-foreground uppercase tracking-wide">
						Browser
					</span>

					<Form {...form}>
						<form
							onSubmit={form.handleSubmit(onSubmit)}
							className="mt-3 space-y-4"
						>
							<FormField
								control={form.control}
								name="chromiumPath"
								render={({ field }) => (
									<FormItem>
										<FormLabel className="text-[11px]">
											Executable path
										</FormLabel>
										<div className="flex gap-2">
											<FormControl>
												<Input
													placeholder="/Applications/Google Chrome.app"
													{...field}
												/>
											</FormControl>
											<Button
												type="button"
												variant="outline"
												className="shrink-0"
												onClick={() => trpc.settings.detectBrowser.mutate()}
											>
												Auto-detect
											</Button>
										</div>
										<FormMessage />
									</FormItem>
								)}
							/>
							<Button type="submit">Save</Button>
						</form>
					</Form>
				</div>

				<div className="h-px bg-[rgba(255,255,255,0.05)]" />

				<div>
					<span className="font-medium text-[10px] text-muted-foreground uppercase tracking-wide">
						Extensions
					</span>

					<ExtensionInstallForm
						onInstall={handleInstall}
						isInstalling={isInstalling}
					/>

					{extensions.length > 0 ? (
						<ExtensionList>
							{extensions.map((ext) => (
								<ExtensionItem
									key={ext.id}
									extension={ext}
									onUninstall={() => setUninstallTarget(ext)}
								/>
							))}
						</ExtensionList>
					) : (
						<p className="mt-4 text-[12px] text-muted-foreground">
							No extensions installed
						</p>
					)}
				</div>

				<div className="h-px bg-[rgba(255,255,255,0.05)]" />

				<div>
					<span className="font-medium text-[10px] text-muted-foreground uppercase tracking-wide">
						Security
					</span>

					<div className="mt-3 space-y-3">
						{pin === null ? (
							<Button
								variant="outline"
								onClick={() =>
									securityStore.getState().showPinScreen(PinScreenMode.SETUP)
								}
							>
								Set up PIN
							</Button>
						) : (
							<div className="flex gap-2">
								<Button
									variant="outline"
									onClick={() =>
										securityStore.getState().showPinScreen(PinScreenMode.CHANGE)
									}
								>
									Change PIN
								</Button>
								<Button
									variant="outline"
									onClick={() =>
										securityStore.getState().showPinScreen(PinScreenMode.REMOVE)
									}
								>
									Remove PIN
								</Button>
							</div>
						)}
					</div>
				</div>

				<UninstallDialog
					extension={uninstallTarget}
					open={uninstallTarget !== null}
					onOpenChange={(open) => {
						if (!open) {
							setUninstallTarget(null);
						}
					}}
					onConfirm={handleUninstall}
				/>
			</div>
		</div>
	);
}
