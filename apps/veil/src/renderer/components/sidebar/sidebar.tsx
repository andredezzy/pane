import { cn } from "@pane/ui/cn";
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
				"flex h-[39px] items-center gap-2 pr-3.5 pl-[82px]",
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
			className={cn("font-semibold text-[#71717a] text-xs", className)}
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
			className={cn("flex-1 overflow-y-auto px-2.5 py-1", className)}
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
		<div
			className={cn("my-1.5 h-px bg-[rgba(255,255,255,0.04)]", className)}
			{...props}
		/>
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
				"flex w-full items-center gap-1.5 rounded-md px-2 py-1.5 text-[#71717a] text-xs transition-colors hover:bg-accent",
				className,
			)}
			{...props}
		>
			<span className="font-light text-sm">+</span>
			<span>New</span>
		</button>
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
					? "bg-[rgba(255,255,255,0.05)] text-accent-foreground"
					: "text-[#71717a] hover:bg-accent hover:text-accent-foreground",
				className,
			)}
			{...props}
		/>
	);
}
