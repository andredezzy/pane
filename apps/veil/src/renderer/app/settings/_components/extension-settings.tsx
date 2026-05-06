import { zodResolver } from "@hookform/resolvers/zod";
import { Button } from "@pane/ui/components/button";
import {
	Dialog,
	DialogContent,
	DialogDescription,
	DialogFooter,
	DialogHeader,
	DialogTitle,
} from "@pane/ui/components/dialog";
import {
	Form,
	FormControl,
	FormField,
	FormItem,
	FormLabel,
	FormMessage,
} from "@pane/ui/components/form";
import { Input } from "@pane/ui/components/input";
import { Loader2 } from "lucide-react";
import { useForm } from "react-hook-form";
import { z } from "zod/v4";

export interface InstalledExtension {
	id: string;
	name: string;
	version: string;
	description: string;
	icon: string;
}

const CWS_URL_PATTERN = /chromewebstore\.google\.com\/.*\/([a-z]{32})/;
const EXTENSION_ID_PATTERN = /^[a-z]{32}$/;

export function parseExtensionId(input: string): string | null {
	const urlMatch = input.match(CWS_URL_PATTERN);

	if (urlMatch) {
		return urlMatch[1];
	}

	if (EXTENSION_ID_PATTERN.test(input)) {
		return input;
	}

	return null;
}

const installSchema = z.object({
	value: z
		.string()
		.min(1, "Enter a Chrome Web Store URL or extension ID")
		.refine(
			(v) => EXTENSION_ID_PATTERN.test(v) || CWS_URL_PATTERN.test(v),
			"Enter a valid Chrome Web Store URL or extension ID",
		),
});

type InstallValues = z.infer<typeof installSchema>;

interface ExtensionInstallFormProps {
	onInstall: (value: string) => void;
	isInstalling: boolean;
}

export function ExtensionInstallForm({
	onInstall,
	isInstalling,
}: ExtensionInstallFormProps) {
	const form = useForm<InstallValues>({
		resolver: zodResolver(installSchema),
		defaultValues: { value: "" },
	});

	const onSubmit = (data: InstallValues) => {
		onInstall(data.value);
		form.reset();
	};

	return (
		<Form {...form}>
			<form onSubmit={form.handleSubmit(onSubmit)} className="mt-3">
				<FormField
					control={form.control}
					name="value"
					render={({ field }) => (
						<FormItem>
							<FormLabel className="text-[11px]">
								Install from Chrome Web Store
							</FormLabel>
							<div className="flex gap-2">
								<FormControl>
									<Input
										placeholder="Paste extension URL or ID"
										disabled={isInstalling}
										{...field}
									/>
								</FormControl>
								<Button
									type="submit"
									className="shrink-0"
									disabled={isInstalling}
								>
									{isInstalling ? (
										<Loader2 className="h-4 w-4 animate-spin" />
									) : (
										"Install"
									)}
								</Button>
							</div>
							<FormMessage />
						</FormItem>
					)}
				/>
			</form>
		</Form>
	);
}

interface ExtensionListProps {
	children: React.ReactNode;
}

export function ExtensionList({ children }: ExtensionListProps) {
	return <div className="mt-4 flex flex-col gap-1.5">{children}</div>;
}

interface ExtensionItemProps {
	extension: InstalledExtension;
	onUninstall: (id: string) => void;
}

export function ExtensionItem({ extension, onUninstall }: ExtensionItemProps) {
	return (
		<div className="flex items-start gap-3 rounded-lg border border-border/50 bg-card/30 p-3">
			{extension.icon ? (
				<img
					src={extension.icon}
					alt=""
					className="h-8 w-8 shrink-0 rounded-md"
				/>
			) : (
				<div className="h-8 w-8 shrink-0 rounded-md bg-muted" />
			)}
			<div className="min-w-0 flex-1">
				<div className="flex items-baseline gap-1.5">
					<span className="font-medium text-[13px] text-accent-foreground">
						{extension.name}
					</span>
					<span className="text-[11px] text-muted-foreground">
						v{extension.version}
					</span>
				</div>
				{extension.description && (
					<p className="mt-0.5 truncate text-[12px] text-muted-foreground">
						{extension.description}
					</p>
				)}
			</div>
			<Button
				variant="ghost"
				size="sm"
				className="shrink-0 text-destructive hover:text-destructive"
				onClick={() => onUninstall(extension.id)}
			>
				Uninstall
			</Button>
		</div>
	);
}

interface UninstallDialogProps {
	extension: InstalledExtension | null;
	open: boolean;
	onOpenChange: (open: boolean) => void;
	onConfirm: () => void;
}

export function UninstallDialog({
	extension,
	open,
	onOpenChange,
	onConfirm,
}: UninstallDialogProps) {
	return (
		<Dialog open={open} onOpenChange={onOpenChange}>
			<DialogContent className="max-w-sm">
				<DialogHeader>
					<DialogTitle>Uninstall {extension?.name}?</DialogTitle>
					<DialogDescription>
						This will remove the extension from all profiles.
					</DialogDescription>
				</DialogHeader>
				<DialogFooter>
					<Button variant="outline" onClick={() => onOpenChange(false)}>
						Cancel
					</Button>
					<Button variant="destructive" onClick={onConfirm}>
						Uninstall
					</Button>
				</DialogFooter>
			</DialogContent>
		</Dialog>
	);
}
