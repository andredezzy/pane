import { cn } from "@pane/ui/cn";
import type React from "react";
import type { CSSProperties, HTMLAttributes } from "react";
import {
	PROFILE_COLOR_HEX,
	type ProfileColor,
} from "../../../constants/profile-colors";

export function ProfileItem({
	className,
	ref,
	style,
	...props
}: React.ComponentPropsWithRef<"div">) {
	return (
		<div
			ref={ref}
			className={cn("mb-1.5", className)}
			style={{ WebkitAppRegion: "no-drag", ...style } as CSSProperties}
			{...props}
		/>
	);
}

interface ProfileHeaderProps extends React.ComponentPropsWithRef<"button"> {
	color: ProfileColor;
	active?: boolean;
}

export function ProfileHeader({
	className,
	color,
	active,
	children,
	ref,
	...props
}: ProfileHeaderProps) {
	const hex = PROFILE_COLOR_HEX[color];

	return (
		<button
			ref={ref}
			type="button"
			className={cn(
				"group flex w-full items-center gap-1.5 overflow-hidden rounded-md px-2 py-1.5 text-xs transition-colors",
				active ? "text-[#d4d4d8]" : "text-[#71717a] hover:bg-accent",
				className,
			)}
			style={
				active
					? {
							border: `1px solid color-mix(in srgb, ${hex} 40%, transparent)`,
							background: `color-mix(in srgb, ${hex} 4%, transparent)`,
						}
					: { border: "1px solid transparent" }
			}
			{...props}
		>
			<div
				className="h-3.5 w-1 shrink-0 rounded-sm"
				style={{ background: hex }}
			/>
			{children}
		</button>
	);
}

export function ProfileName({
	className,
	...props
}: HTMLAttributes<HTMLSpanElement>) {
	return (
		<span
			className={cn("flex-1 truncate text-left font-medium text-xs", className)}
			{...props}
		/>
	);
}

export function ProfileBadge({
	className,
	...props
}: HTMLAttributes<HTMLSpanElement>) {
	return (
		<span className={cn("text-[#71717a] text-[10px]", className)} {...props} />
	);
}

export function ProfileTabs({
	className,
	...props
}: HTMLAttributes<HTMLDivElement>) {
	return <div className={cn("mt-1.5 space-y-1", className)} {...props} />;
}
