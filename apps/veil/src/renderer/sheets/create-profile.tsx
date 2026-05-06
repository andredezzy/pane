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
import {
	Select,
	SelectContent,
	SelectItem,
	SelectTrigger,
	SelectValue,
} from "@pane/ui/components/select";
import { Separator } from "@pane/ui/components/separator";
import {
	Sheet,
	SheetBody,
	SheetContent,
	SheetFooter,
	SheetHeader,
	SheetTitle,
} from "@pane/ui/components/sheet";
import { Switch } from "@pane/ui/components/switch";
import { X } from "lucide-react";
import { useEffect, useState } from "react";
import { useForm } from "react-hook-form";
import { z } from "zod/v4";
import { ProfileColor } from "../../constants/profile-colors";
import { Platform, ProxyType, profileStore } from "../../stores/profile-store";
import { ColorPicker } from "../components/color-picker";
import { DEFAULT_FINGERPRINTS } from "../components/default-fingerprints";
import { SHEET_ANIMATION_MS } from "../constants";

const formSchema = z.object({
	name: z.string().min(1, "Profile name is required"),
	group: z.string().optional(),
	color: z.nativeEnum(ProfileColor),
	platform: z.nativeEnum(Platform),
	proxyEnabled: z.boolean(),
	proxy: z
		.object({
			proxyType: z.nativeEnum(ProxyType),
			host: z.string(),
			port: z.number().int().min(1).max(65535),
			username: z.string().optional(),
			password: z.string().optional(),
		})
		.optional(),
});

type FormValues = z.infer<typeof formSchema>;

function detectPlatform(): Platform {
	const userAgent = navigator.userAgent.toLowerCase();

	if (userAgent.includes("mac")) {
		return Platform.MACOS;
	}

	return userAgent.includes("linux") ? Platform.LINUX : Platform.WINDOWS;
}

interface Props {
	onClose: () => void;
}

export function CreateProfileSheet({ onClose }: Props) {
	const [open, setOpen] = useState(false);

	useEffect(() => {
		requestAnimationFrame(() => setOpen(true));
	}, []);

	const close = () => {
		setOpen(false);
		setTimeout(onClose, SHEET_ANIMATION_MS);
	};

	const form = useForm<FormValues>({
		resolver: zodResolver(formSchema),
		defaultValues: {
			name: "",
			group: "",
			color: ProfileColor.BLUE,
			platform: detectPlatform(),
			proxyEnabled: false,
			proxy: {
				proxyType: ProxyType.HTTP,
				host: "",
				port: 8080,
				username: "",
				password: "",
			},
		},
	});

	const proxyEnabled = form.watch("proxyEnabled");

	const onSubmit = (data: FormValues) => {
		profileStore.getState().create({
			name: data.name,
			color: data.color,
			group: data.group || null,
			notes: null,
			fingerprint: DEFAULT_FINGERPRINTS[data.platform],
			proxy:
				data.proxyEnabled && data.proxy?.host
					? {
							proxyType: data.proxy.proxyType,
							host: data.proxy.host,
							port: data.proxy.port,
							username: data.proxy.username || null,
							password: data.proxy.password || null,
						}
					: null,
			tags: [],
		});

		form.reset();
		close();
	};

	return (
		<Sheet open={open} onOpenChange={(open) => !open && close()}>
			<SheetContent>
				<SheetHeader>
					<SheetTitle>New profile</SheetTitle>
					<button
						type="button"
						onClick={close}
						className="flex h-6 w-6 items-center justify-center rounded text-[#71717a] transition-colors hover:text-foreground"
					>
						<X className="h-4 w-4" />
					</button>
				</SheetHeader>
				<Form {...form}>
					<form
						onSubmit={form.handleSubmit(onSubmit)}
						className="flex flex-1 flex-col"
					>
						<SheetBody className="space-y-4">
							<FormField
								control={form.control}
								name="name"
								render={({ field }) => (
									<FormItem>
										<FormLabel className="text-[11px]">Name</FormLabel>
										<FormControl>
											<Input
												placeholder="e.g. Work account"
												autoFocus
												{...field}
											/>
										</FormControl>
										<FormMessage />
									</FormItem>
								)}
							/>
							<FormField
								control={form.control}
								name="group"
								render={({ field }) => (
									<FormItem>
										<FormLabel className="text-[11px]">
											Group <span className="text-[#71717a]">(optional)</span>
										</FormLabel>
										<FormControl>
											<Input placeholder="Enter group name" {...field} />
										</FormControl>
									</FormItem>
								)}
							/>
							<FormField
								control={form.control}
								name="color"
								render={({ field }) => (
									<FormItem>
										<FormLabel className="text-[11px]">Color</FormLabel>
										<FormControl>
											<ColorPicker
												value={field.value}
												onChange={field.onChange}
											/>
										</FormControl>
									</FormItem>
								)}
							/>
							<FormField
								control={form.control}
								name="platform"
								render={({ field }) => (
									<FormItem>
										<FormLabel className="text-[11px]">Platform</FormLabel>
										<div className="flex gap-1">
											{([Platform.WINDOWS, Platform.MACOS, Platform.LINUX] as const).map((platform) => (
												<Button
													key={platform}
													type="button"
													variant={field.value === platform ? "default" : "outline"}
													className="h-8 flex-1 text-[11px] capitalize"
													onClick={() => field.onChange(platform)}
												>
													{platform.toLowerCase()}
												</Button>
											))}
										</div>
									</FormItem>
								)}
							/>
							<Separator />
							<FormField
								control={form.control}
								name="proxyEnabled"
								render={({ field }) => (
									<FormItem className="flex flex-row items-center justify-between">
										<FormLabel className="text-[11px]">Enable proxy</FormLabel>
										<FormControl>
											<Switch
												checked={field.value}
												onCheckedChange={field.onChange}
											/>
										</FormControl>
									</FormItem>
								)}
							/>
							{proxyEnabled ? (
								<div className="space-y-3 rounded-md border border-border p-3">
									<div className="flex gap-2">
										<FormField
											control={form.control}
											name="proxy.proxyType"
											render={({ field }) => (
												<FormItem>
													<Select
														onValueChange={field.onChange}
														defaultValue={field.value}
													>
														<FormControl>
															<SelectTrigger className="w-[100px]">
																<SelectValue />
															</SelectTrigger>
														</FormControl>
														<SelectContent>
															<SelectItem value={ProxyType.HTTP}>HTTP</SelectItem>
															<SelectItem value={ProxyType.HTTPS}>HTTPS</SelectItem>
															<SelectItem value={ProxyType.SOCKS4}>SOCKS4</SelectItem>
															<SelectItem value={ProxyType.SOCKS5}>SOCKS5</SelectItem>
														</SelectContent>
													</Select>
												</FormItem>
											)}
										/>
										<FormField
											control={form.control}
											name="proxy.host"
											render={({ field }) => (
												<FormItem className="flex-1">
													<FormControl>
														<Input placeholder="Host" {...field} />
													</FormControl>
													<FormMessage />
												</FormItem>
											)}
										/>
										<FormField
											control={form.control}
											name="proxy.port"
											render={({ field }) => (
												<FormItem className="w-20">
													<FormControl>
														<Input
															type="number"
															placeholder="Port"
															{...field}
															onChange={(e) =>
																field.onChange(Number(e.target.value))
															}
														/>
													</FormControl>
												</FormItem>
											)}
										/>
									</div>
									<div className="flex gap-2">
										<FormField
											control={form.control}
											name="proxy.username"
											render={({ field }) => (
												<FormItem className="flex-1">
													<FormControl>
														<Input
															placeholder="Username (optional)"
															{...field}
														/>
													</FormControl>
												</FormItem>
											)}
										/>
										<FormField
											control={form.control}
											name="proxy.password"
											render={({ field }) => (
												<FormItem className="flex-1">
													<FormControl>
														<Input
															type="password"
															placeholder="Password (optional)"
															{...field}
														/>
													</FormControl>
												</FormItem>
											)}
										/>
									</div>
								</div>
							) : null}
						</SheetBody>
						<SheetFooter>
							<Button
								type="button"
								variant="outline"
								onClick={() => {
									form.reset();
									close();
								}}
							>
								Cancel
							</Button>
							<Button type="submit">Create</Button>
						</SheetFooter>
					</form>
				</Form>
			</SheetContent>
		</Sheet>
	);
}
