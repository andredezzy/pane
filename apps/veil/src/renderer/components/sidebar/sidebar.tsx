import { cn } from "@pane/ui/cn";
import { X } from "lucide-react";
import type { CSSProperties, HTMLAttributes } from "react";

export function Sidebar({ className, ...props }: HTMLAttributes<HTMLElement>) {
	return (
		<aside
			className={cn("flex w-[220px] shrink-0 flex-col", className)}
			{...props}
		/>
	);
}

export function SidebarHeader({
	className,
	...props
}: HTMLAttributes<HTMLDivElement>) {
	return (
		<div
			className={cn(
				"flex h-[39px] items-center gap-2 pr-2.5 pl-[82px]",
				className,
			)}
			{...props}
		/>
	);
}

export function SidebarTitle({
	className,
	...props
}: HTMLAttributes<HTMLSpanElement>) {
	return (
		<span
			className={cn("font-semibold text-muted-foreground text-xs", className)}
			{...props}
		/>
	);
}

export function SidebarContent({
	className,
	style,
	...props
}: HTMLAttributes<HTMLDivElement>) {
	return (
		<div
			// scroll-fade-y fades the top/bottom edges as content scrolls under the
			// header/footer — and only toward content that exists (no fade at the very
			// top or bottom, none when everything fits). See globals.css.
			className={cn(
				"scroll-fade-y flex-1 overflow-y-auto px-2.5 py-1",
				className,
			)}
			// Opt the scroll area out of the window-drag region — otherwise macOS
			// treats the gaps between items as a title-bar and swallows the wheel
			// events, so scrolling only works while hovering an item.
			style={{ WebkitAppRegion: "no-drag", ...style } as CSSProperties}
			{...props}
		/>
	);
}

export function SidebarFooter({
	className,
	...props
}: HTMLAttributes<HTMLDivElement>) {
	return <div className={cn("px-2.5 pb-2", className)} {...props} />;
}

export function SidebarSeparator({
	className,
	...props
}: HTMLAttributes<HTMLDivElement>) {
	return (
		<div className={cn("my-1.5 h-px bg-foreground/4", className)} {...props} />
	);
}

export function SidebarNewButton({
	className,
	...props
}: HTMLAttributes<HTMLButtonElement>) {
	return (
		<button
			type="button"
			className={cn(
				"flex w-full items-center gap-1.5 rounded-md px-2 py-1.5 text-muted-foreground text-xs transition-colors hover:bg-accent",
				className,
			)}
			{...props}
		>
			<span className="font-light text-sm">+</span>
			<span>New</span>
		</button>
	);
}

interface SidebarUpdatePillProps {
	onOpen: () => void;
	onDismiss: () => void;
	className?: string;
}

// Quiet by design: no color beyond a single status dot, no motion beyond a
// soft entrance — this announces an update, it never demands attention.
export function SidebarUpdatePill({
	className,
	onOpen,
	onDismiss,
}: SidebarUpdatePillProps) {
	return (
		<div
			className={cn(
				"group fade-in-0 slide-in-from-bottom-1 relative mb-1.5 flex animate-in items-center gap-1.5 rounded-md bg-[rgba(255,255,255,0.05)] py-1.5 pr-6 pl-2 duration-300",
				className,
			)}
		>
			<button
				type="button"
				onClick={onOpen}
				className="flex flex-1 items-center gap-1.5 overflow-hidden text-left text-[#d4d4d8] text-xs"
			>
				<span className="h-1.5 w-1.5 shrink-0 rounded-full bg-emerald-500" />
				<span className="truncate">Update available</span>
			</button>

			<button
				type="button"
				onClick={onDismiss}
				aria-label="Dismiss"
				className="-translate-y-1/2 absolute top-1/2 right-1.5 flex h-4 w-4 items-center justify-center rounded text-[#71717a] opacity-0 transition-opacity hover:text-accent-foreground group-hover:opacity-100"
			>
				<X className="h-3 w-3" />
			</button>
		</div>
	);
}

export function SidebarSettingsButton({
	className,
	active,
	...props
}: HTMLAttributes<HTMLButtonElement> & { active?: boolean }) {
	return (
		<button
			type="button"
			className={cn(
				"flex w-full items-center gap-1.5 rounded-md px-2 py-1.5 text-xs transition-colors",
				active
					? "bg-foreground/5 text-accent-foreground"
					: "text-muted-foreground hover:bg-accent hover:text-accent-foreground",
				className,
			)}
			{...props}
		/>
	);
}
